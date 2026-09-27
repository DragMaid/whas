using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using Microsoft.AspNetCore.Mvc.Testing;
using Testcontainers.PostgreSql;

namespace Whas.Server.Tests;

// One Postgres container and one server for all integration tests, with
// short match deadlines so timeouts can be exercised quickly
public sealed class ServerFixture : IAsyncLifetime
{
    readonly PostgreSqlContainer _db = new PostgreSqlBuilder()
        .WithImage("postgres:17-alpine").Build();

    public WebApplicationFactory<Program> Factory { get; private set; } = null!;

    public async Task InitializeAsync()
    {
        await _db.StartAsync();
        Environment.SetEnvironmentVariable("ConnectionStrings__Whas", _db.GetConnectionString());
        Environment.SetEnvironmentVariable("Match__DeckDeadline", "00:00:05");
        Environment.SetEnvironmentVariable("Match__PlanDeadline", "00:00:02");
        Environment.SetEnvironmentVariable("Match__RevealDeadline", "00:00:02");
        Environment.SetEnvironmentVariable("Match__HashDeadline", "00:00:02");
        Environment.SetEnvironmentVariable("Match__SnapshotDeadline", "00:00:02");
        Environment.SetEnvironmentVariable("Match__RejoinGrace", "00:00:02");
        Factory = new WebApplicationFactory<Program>();
        _ = Factory.Server; // start (and migrate) now
    }

    public async Task DisposeAsync()
    {
        await Factory.DisposeAsync();
        await _db.DisposeAsync();
    }
}

[CollectionDefinition("server")]
public sealed class ServerCollection : ICollectionFixture<ServerFixture>;

// A scripted game client over the real WebSocket endpoint
public sealed class TestClient : IAsyncDisposable
{
    readonly WebSocket _ws;
    readonly Channel<JsonElement> _inbox = Channel.CreateUnbounded<JsonElement>();
    readonly List<JsonElement> _unread = [];
    readonly CancellationTokenSource _cts = new();
    readonly Task _receiver;

    public long PlayerId { get; private set; }
    public string Token { get; private set; } = "";
    public JsonElement Welcome { get; private set; }

    TestClient(WebSocket ws)
    {
        _ws = ws;
        _receiver = ReceiveLoopAsync();
    }

    public static async Task<TestClient> ConnectAsync(ServerFixture server, string buildId,
                                                      string? token = null)
    {
        var wsClient = server.Factory.Server.CreateWebSocketClient();
        var ws = await wsClient.ConnectAsync(new Uri(server.Factory.Server.BaseAddress, "ws"),
                                             CancellationToken.None);
        var client = new TestClient(ws);
        await client.SendAsync(new { type = "hello", buildId, token, protocol = 1 });
        var welcome = await client.ExpectAsync("welcome");
        client.Welcome = welcome;
        client.PlayerId = welcome.GetProperty("playerId").GetInt64();
        client.Token = token ?? welcome.GetProperty("token").GetString()!;
        return client;
    }

    public Task SendAsync(object message) =>
        _ws.SendAsync(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(message)),
                      WebSocketMessageType.Text, true, CancellationToken.None);

    // Waits for the next message of this type; others are kept for later
    public async Task<JsonElement> ExpectAsync(string type, double seconds = 10)
    {
        int i = _unread.FindIndex(m => m.GetProperty("type").GetString() == type);
        if (i >= 0)
        {
            var found = _unread[i];
            _unread.RemoveAt(i);
            return found;
        }
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
        try
        {
            while (true)
            {
                var msg = await _inbox.Reader.ReadAsync(timeout.Token);
                if (msg.GetProperty("type").GetString() == type)
                    return msg;
                _unread.Add(msg);
            }
        }
        catch (OperationCanceledException)
        {
            throw new TimeoutException(
                $"no '{type}' within {seconds}s; got: " +
                string.Join(", ", _unread.Select(m => m.GetProperty("type").GetString())));
        }
    }

    async Task ReceiveLoopAsync()
    {
        var buffer = new byte[64 * 1024];
        using var frame = new MemoryStream();
        try
        {
            while (_ws.State == WebSocketState.Open)
            {
                var r = await _ws.ReceiveAsync(buffer, _cts.Token);
                if (r.MessageType == WebSocketMessageType.Close)
                    break;
                frame.Write(buffer, 0, r.Count);
                if (!r.EndOfMessage)
                    continue;
                using var doc = JsonDocument.Parse(frame.ToArray());
                frame.SetLength(0);
                await _inbox.Writer.WriteAsync(doc.RootElement.Clone());
            }
        }
        catch (Exception e) when (e is WebSocketException or OperationCanceledException or ObjectDisposedException) { }
        _inbox.Writer.TryComplete();
    }

    public async ValueTask DisposeAsync()
    {
        try
        {
            // Send our close; the receive loop picks up the server's reply
            if (_ws.State == WebSocketState.Open)
                await _ws.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None);
            await _receiver.WaitAsync(TimeSpan.FromSeconds(5));
        }
        catch (Exception e) when (e is WebSocketException or TimeoutException or ObjectDisposedException) { }
        _cts.Cancel();
        _ws.Dispose();
    }
}
