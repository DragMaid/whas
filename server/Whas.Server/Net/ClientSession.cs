using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Channels;
using Whas.Server.Data;

namespace Whas.Server.Net;

// One client's WebSocket. Messages are JSON objects with a "type"; outgoing
// ones are queued and written by a single sender so writes never interleave.
public sealed class ClientSession(WebSocket socket, ILogger logger)
{
    // Room for a world snapshot during desync recovery
    public const int MaxMessageBytes = 1024 * 1024;

    readonly Channel<string> _outbox = Channel.CreateBounded<string>(
        new BoundedChannelOptions(512) { FullMode = BoundedChannelFullMode.DropOldest });

    public Guid Id { get; } = Guid.NewGuid();
    public Player? Player { get; set; }
    public string? BuildId { get; set; }
    public bool Closed { get; private set; }

    // Set while the session plays in a match
    public Matches.MatchActor? Match { get; set; }
    public int Slot { get; set; } = -1;

    public void Send(JsonObject message)
    {
        if (!Closed)
            _outbox.Writer.TryWrite(message.ToJsonString());
    }

    public void Send(string type, object? payload = null)
    {
        var node = payload is null
            ? new JsonObject()
            : JsonSerializer.SerializeToNode(payload, Protocol.Json)!.AsObject();
        node["type"] = type;
        Send(node);
    }

    public void Error(string message) => Send("error", new { message });

    // Runs until the socket closes. onMessage gets each parsed message.
    public async Task RunAsync(Func<ClientSession, JsonElement, Task> onMessage, CancellationToken ct)
    {
        var sender = SendLoopAsync(ct);
        var buffer = new byte[16 * 1024];
        using var frame = new MemoryStream();
        try
        {
            while (socket.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                var result = await socket.ReceiveAsync(buffer, ct);
                if (result.MessageType == WebSocketMessageType.Close)
                    break;
                frame.Write(buffer, 0, result.Count);
                if (frame.Length > MaxMessageBytes)
                {
                    Error("message too large");
                    break;
                }
                if (!result.EndOfMessage)
                    continue;

                JsonDocument? doc = null;
                try { doc = JsonDocument.Parse(frame.ToArray()); }
                catch (JsonException) { Error("message is not JSON"); }
                frame.SetLength(0);
                if (doc is null)
                    continue;
                using (doc)
                {
                    if (doc.RootElement.ValueKind != JsonValueKind.Object ||
                        !doc.RootElement.TryGetProperty("type", out _))
                    {
                        Error("message has no type");
                        continue;
                    }
                    try { await onMessage(this, doc.RootElement); }
                    catch (Exception e) when (e is JsonException or InvalidOperationException or FormatException or KeyNotFoundException)
                    {
                        logger.LogDebug(e, "bad message from {Session}", Id);
                        Error("malformed message");
                    }
                }
            }
        }
        catch (WebSocketException) { }
        catch (OperationCanceledException) { }
        finally
        {
            Closed = true;
            _outbox.Writer.TryComplete();
            await sender;
        }
    }

    async Task SendLoopAsync(CancellationToken ct)
    {
        try
        {
            await foreach (var text in _outbox.Reader.ReadAllAsync(ct))
            {
                if (socket.State != WebSocketState.Open)
                    break;
                await socket.SendAsync(Encoding.UTF8.GetBytes(text), WebSocketMessageType.Text, true, ct);
            }
            // Finish the close handshake, whichever side started it
            if (socket.State is WebSocketState.Open or WebSocketState.CloseReceived)
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
                await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, null, timeout.Token);
            }
        }
        catch (WebSocketException) { }
        catch (OperationCanceledException) { }
    }
}
