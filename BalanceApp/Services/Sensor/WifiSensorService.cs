using System;
using System.IO;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BalanceApp.Services.Sensor;

public class WifiSensorService : ISensorService
{
    private TcpClient? _tcpClient;
    private StreamReader? _reader;
    private Thread? _readThread;
    private bool _keepReading;

    public event Action<SensorData>? DataReceived;
    public event Action<string>? StatusChanged;

    public bool IsConnected => _tcpClient != null && _tcpClient.Connected;
    public bool IsSimulationMode { get; private set; }

    public string Host { get; set; } = "esp32-balance.local";
    public int Port { get; set; } = 8888;

    public void ToggleSimulation(bool enable)
    {
        IsSimulationMode = enable;
    }

    public void Connect(string connectionString)
    {
        // Format: "esp32-balance.local:8888" or "192.168.1.50:8888" or "esp32-balance.local"
        if (!string.IsNullOrWhiteSpace(connectionString))
        {
            var parts = connectionString.Split(':');
            Host = parts[0].Trim();
            if (parts.Length > 1 && int.TryParse(parts[1].Trim(), out int parsedPort))
            {
                Port = parsedPort;
            }
        }

        Connect(Host, Port);
    }

    public void Connect(string host, int port)
    {
        Disconnect();
        Host = host;
        Port = port;

        Task.Run(async () =>
        {
            try
            {
                StatusChanged?.Invoke($"[WiFi] Đang kết nối {Host}:{Port}...");
                var client = new TcpClient();
                
                // Connect with 5s timeout
                var connectTask = client.ConnectAsync(Host, Port);
                var timeoutTask = Task.Delay(5000);
                var completedTask = await Task.WhenAny(connectTask, timeoutTask);

                if (completedTask == timeoutTask)
                {
                    client.Close();
                    StatusChanged?.Invoke($"[WiFi] Hết thời gian chờ kết nối ({Host}:{Port})");
                    return;
                }

                await connectTask;
                _tcpClient = client;
                _reader = new StreamReader(_tcpClient.GetStream());
                _keepReading = true;

                _readThread = new Thread(ReadStream) { IsBackground = true };
                _readThread.Start();

                StatusChanged?.Invoke($"[WiFi] Đã kết nối ({Host}:{Port})");
            }
            catch (Exception ex)
            {
                StatusChanged?.Invoke($"[WiFi] Lỗi kết nối: {ex.Message}");
                Disconnect();
            }
        });
    }

    public void Disconnect()
    {
        _keepReading = false;
        try { _reader?.Dispose(); } catch { }
        try { _tcpClient?.Close(); _tcpClient?.Dispose(); } catch { }
        _reader = null;
        _tcpClient = null;

        StatusChanged?.Invoke("[WiFi] Đã ngắt kết nối");
    }

    public async Task<bool> ScanAndConnectAsync()
    {
        StatusChanged?.Invoke($"[WiFi] Đang tìm kiếm thiết bị tại {Host}:{Port}...");
        try
        {
            using var testClient = new TcpClient();
            var connectTask = testClient.ConnectAsync(Host, Port);
            var timeoutTask = Task.Delay(3000);
            if (await Task.WhenAny(connectTask, timeoutTask) == connectTask)
            {
                testClient.Close();
                Connect(Host, Port);
                return true;
            }
        }
        catch { }

        StatusChanged?.Invoke("[WiFi] Không tìm thấy thiết bị qua WiFi");
        return false;
    }

    public void StartReading() { }
    public void StopReading() { Disconnect(); }

    private void ReadStream()
    {
        while (_keepReading && _tcpClient != null && _tcpClient.Connected)
        {
            try
            {
                string? line = _reader?.ReadLine();
                if (string.IsNullOrWhiteSpace(line)) continue;

                ParseLine(line);
            }
            catch (Exception)
            {
                break;
            }
        }

        if (_keepReading)
        {
            StatusChanged?.Invoke("[WiFi] Mất kết nối");
            Disconnect();
        }
    }

    private void ParseLine(string line)
    {
        try
        {
            int startIndex = line.IndexOf('{');
            int endIndex = line.LastIndexOf('}');

            if (startIndex != -1 && endIndex != -1 && endIndex > startIndex)
            {
                string json = line.Substring(startIndex, endIndex - startIndex + 1);
                var data = JsonSerializer.Deserialize<SensorDataRaw>(json);

                if (data != null)
                {
                    var sensorData = new SensorData
                    {
                        Timestamp = data.ts,
                        X = data.x,
                        Y = data.y,
                        Force1 = data.f1,
                        Force2 = data.f2,
                        Force3 = data.f3,
                        Force4 = data.f4
                    };
                    DataReceived?.Invoke(sensorData);
                }
            }
        }
        catch { }
    }

    private class SensorDataRaw
    {
        public long ts { get; set; }
        public float x { get; set; }
        public float y { get; set; }
        public float f1 { get; set; }
        public float f2 { get; set; }
        public float f3 { get; set; }
        public float f4 { get; set; }
    }
}
