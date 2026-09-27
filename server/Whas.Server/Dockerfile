FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src
COPY Whas.Server/Whas.Server.csproj Whas.Server/
RUN dotnet restore Whas.Server/Whas.Server.csproj
COPY Whas.Server/ Whas.Server/
RUN dotnet publish Whas.Server/Whas.Server.csproj -c Release -o /app --no-restore

FROM mcr.microsoft.com/dotnet/aspnet:10.0
WORKDIR /app
COPY --from=build /app .
ENV ASPNETCORE_URLS=http://+:8080
EXPOSE 8080
ENTRYPOINT ["dotnet", "Whas.Server.dll"]
