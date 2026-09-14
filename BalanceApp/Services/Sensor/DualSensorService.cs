using System;
using System.Threading.Tasks;

namespace BalanceApp.Services.Sensor;

public enum ConnectionChannelMode
{
    Wifi,
    Bluetooth,
    Both
}

public class DualSensorService : ISensorService
{
    public SerialSensorService BluetoothService { get; }
    public WifiSensorService WifiService { get; }

    public ConnectionChannelMode Mode { get; set; } = ConnectionChannelMode.Wifi;

    public event Action<SensorData>? DataReceived;
    public event Action<string>? StatusChanged;

    public bool IsWifiConnected => WifiService.IsConnected;
    public bool IsBluetoothConnected => BluetoothService.IsConnected;

    public bool IsConnected => IsSimulationMode || (Mode switch
    {
        ConnectionChannelMode.Wifi => IsWifiConnected,
        ConnectionChannelMode.Bluetooth => IsBluetoothConnected,
        ConnectionChannelMode.Both => IsWifiConnected || IsBluetoothConnected,
        _ => false
    });

    public bool IsSimulationMode => BluetoothService.IsSimulationMode;

    public string WifiStatus { get; private set; } = "Chưa kết nối";
    public string BluetoothStatus { get; private set; } = "Chưa kết nối";

    private long _lastReceivedTimestamp = -1;
    private readonly object _lock = new();

    public DualSensorService()
    {
        BluetoothService = new SerialSensorService();
        WifiService = new WifiSensorService();

        BluetoothService.DataReceived += OnDataReceivedFromChannel;
        WifiService.DataReceived += OnDataReceivedFromChannel;

        BluetoothService.StatusChanged += status =>
        {
            BluetoothStatus = status;
            NotifyStatus();
        };

        WifiService.StatusChanged += status =>
        {
            WifiStatus = status;
            NotifyStatus();
        };
    }

    private void OnDataReceivedFromChannel(SensorData data)
    {
        lock (_lock)
        {
            // If both channels are active, deduplicate identical timestamp packets
            if (data.Timestamp == _lastReceivedTimestamp && _lastReceivedTimestamp != -1)
            {
                return;
            }
            _lastReceivedTimestamp = data.Timestamp;
        }

        DataReceived?.Invoke(data);
    }

    private void NotifyStatus()
    {
        string statusText = Mode switch
        {
            ConnectionChannelMode.Wifi => WifiStatus,
            ConnectionChannelMode.Bluetooth => BluetoothStatus,
            ConnectionChannelMode.Both => $"WiFi: {WifiStatus} | BT: {BluetoothStatus}",
            _ => "Chưa kết nối"
        };
        StatusChanged?.Invoke(statusText);
    }

    public void ToggleSimulation(bool enable)
    {
        BluetoothService.ToggleSimulation(enable);
        NotifyStatus();
    }

    public void Connect(string connectionString)
    {
        switch (Mode)
        {
            case ConnectionChannelMode.Wifi:
                WifiService.Connect(connectionString);
                break;
            case ConnectionChannelMode.Bluetooth:
                BluetoothService.Connect(connectionString);
                break;
            case ConnectionChannelMode.Both:
                // connectionString may be "host:port;COMx" or use service defaults
                var parts = connectionString.Split(';');
                if (parts.Length > 0 && !string.IsNullOrWhiteSpace(parts[0]))
                    WifiService.Connect(parts[0]);
                if (parts.Length > 1 && !string.IsNullOrWhiteSpace(parts[1]))
                    BluetoothService.Connect(parts[1]);
                break;
        }
    }

    public void ConnectWifi(string host, int port)
    {
        WifiService.Connect(host, port);
    }

    public void DisconnectWifi()
    {
        WifiService.Disconnect();
    }

    public void ConnectBluetooth(string portName)
    {
        BluetoothService.Connect(portName);
    }

    public void DisconnectBluetooth()
    {
        BluetoothService.Disconnect();
    }

    public async Task<bool> ScanAndConnectAsync()
    {
        bool success = false;
        if (Mode == ConnectionChannelMode.Wifi || Mode == ConnectionChannelMode.Both)
        {
            bool wifiOk = await WifiService.ScanAndConnectAsync();
            if (wifiOk) success = true;
        }

        if (Mode == ConnectionChannelMode.Bluetooth || Mode == ConnectionChannelMode.Both)
        {
            bool btOk = await BluetoothService.ScanAndConnectAsync();
            if (btOk) success = true;
        }

        return success;
    }

    public void Disconnect()
    {
        WifiService.Disconnect();
        BluetoothService.Disconnect();
        _lastReceivedTimestamp = -1;
    }

    public void StartReading()
    {
        WifiService.StartReading();
        BluetoothService.StartReading();
    }

    public void StopReading()
    {
        WifiService.StopReading();
        BluetoothService.StopReading();
    }
}
