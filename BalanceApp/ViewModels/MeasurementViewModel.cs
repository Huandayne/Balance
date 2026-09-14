using System;
using System.Collections.ObjectModel;
using System.IO.Ports;
using System.Linq;
using System.Threading.Tasks;
using BalanceApp.Models;
using BalanceApp.Services.Sensor;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ScottPlot;
using System.Collections.Concurrent;
using System.Collections.Generic;
using Avalonia.Threading;

namespace BalanceApp.ViewModels;

public partial class MeasurementViewModel : ViewModelBase
{
    private readonly MainViewModel _mainViewModel;
    private ISensorService _sensorService => _mainViewModel.SensorService;

    private DualSensorService? DualService => _sensorService as DualSensorService;

    [ObservableProperty]
    private string _title = "Đo Thăng Bằng";

    [ObservableProperty]
    private string _connectionStatus = "Chưa kết nối";

    // 0: WiFi, 1: Bluetooth, 2: Cả hai
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsWifiModeActive))]
    [NotifyPropertyChangedFor(nameof(IsBluetoothModeActive))]
    private int _selectedModeIndex = 0; // Mặc định là WiFi

    public bool IsWifiModeActive => SelectedModeIndex == 0 || SelectedModeIndex == 2;
    public bool IsBluetoothModeActive => SelectedModeIndex == 1 || SelectedModeIndex == 2;

    partial void OnSelectedModeIndexChanged(int value)
    {
        if (DualService != null)
        {
            DualService.Mode = value switch
            {
                0 => ConnectionChannelMode.Wifi,
                1 => ConnectionChannelMode.Bluetooth,
                2 => ConnectionChannelMode.Both,
                _ => ConnectionChannelMode.Wifi
            };
        }
        UpdateConnectionFlags();
    }

    // WiFi settings & status
    [ObservableProperty]
    private string _wifiHost = "esp32-balance.local";

    [ObservableProperty]
    private int _wifiPort = 8888;

    [ObservableProperty]
    private bool _isWifiConnected;

    [ObservableProperty]
    private string _wifiStatus = "Chưa kết nối";

    // Bluetooth settings & status
    [ObservableProperty]
    private ObservableCollection<string> _availablePorts = new();

    [ObservableProperty]
    private string? _selectedPort;

    [ObservableProperty]
    private bool _isBluetoothConnected;

    [ObservableProperty]
    private string _bluetoothStatus = "Chưa kết nối";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ConnectionButtonText))]
    private bool _isConnected;

    [ObservableProperty]
    private bool _isMeasuring;

    public string ConnectionButtonText => IsConnected ? "Ngắt Kết Nối" : "Kết Nối";
    public string WifiButtonText => IsWifiConnected ? "Ngắt WiFi" : "Nối WiFi";
    public string BluetoothButtonText => IsBluetoothConnected ? "Ngắt BT" : "Nối BT";
    
    [ObservableProperty]
    private string _actionButtonText = "BẮT ĐẦU ĐO";

    [ObservableProperty]
    private bool _canStart = true; // Button enabled state

    private List<TestSample> _currentSamples = new();

    public Patient? CurrentPatient => _mainViewModel.SelectedPatient;
    public MainViewModel MainContext => _mainViewModel;

    public MeasurementViewModel(MainViewModel mainViewModel)
    {
        _mainViewModel = mainViewModel;
        
        // Load COM ports
        RefreshPorts();
        
        _sensorService.StatusChanged += OnStatusChanged;
        _sensorService.DataReceived += OnDataReceived;
        
        // Auto-scan immediately
        Task.Run(async () => await ScanDevices());
    }

    private void UpdateConnectionFlags()
    {
        if (DualService != null)
        {
            IsWifiConnected = DualService.IsWifiConnected;
            IsBluetoothConnected = DualService.IsBluetoothConnected;
            WifiStatus = DualService.WifiStatus;
            BluetoothStatus = DualService.BluetoothStatus;
            IsConnected = DualService.IsConnected;
        }
        else
        {
            IsConnected = _sensorService.IsConnected;
        }
        OnPropertyChanged(nameof(WifiButtonText));
        OnPropertyChanged(nameof(BluetoothButtonText));
    }

    private void OnStatusChanged(string status)
    {
        ConnectionStatus = status;
        UpdateConnectionFlags();
    }

    [RelayCommand]
    private async Task ScanDevices()
    {
        await _sensorService.ScanAndConnectAsync();
        UpdateConnectionFlags();
    }

    [RelayCommand]
    private void RefreshPorts()
    {
        var ports = SerialPort.GetPortNames().ToList();
        AvailablePorts = new ObservableCollection<string>(ports);
        if (SelectedPort == null && ports.Any())
            SelectedPort = ports.First();
    }

    [RelayCommand]
    private void ToggleWifiConnection()
    {
        if (DualService != null)
        {
            if (DualService.IsWifiConnected)
            {
                DualService.DisconnectWifi();
            }
            else
            {
                DualService.ConnectWifi(WifiHost, WifiPort);
            }
        }
        else if (IsConnected)
        {
            _sensorService.Disconnect();
        }
        else
        {
            _sensorService.Connect($"{WifiHost}:{WifiPort}");
        }
        UpdateConnectionFlags();
    }

    [RelayCommand]
    private void ToggleBluetoothConnection()
    {
        if (string.IsNullOrEmpty(SelectedPort)) return;

        if (DualService != null)
        {
            if (DualService.IsBluetoothConnected)
            {
                DualService.DisconnectBluetooth();
            }
            else
            {
                DualService.ConnectBluetooth(SelectedPort);
            }
        }
        else if (IsConnected)
        {
            _sensorService.Disconnect();
        }
        else
        {
            _sensorService.Connect(SelectedPort);
        }
        UpdateConnectionFlags();
    }

    [RelayCommand]
    private void ToggleConnection()
    {
        if (IsConnected)
        {
            _sensorService.Disconnect();
            ConnectionStatus = "Đã ngắt kết nối";
            IsMeasuring = false;
        }
        else
        {
            if (DualService != null)
            {
                if (IsWifiModeActive)
                {
                    DualService.ConnectWifi(WifiHost, WifiPort);
                }
                if (IsBluetoothModeActive && !string.IsNullOrEmpty(SelectedPort))
                {
                    DualService.ConnectBluetooth(SelectedPort);
                }
            }
            else
            {
                if (IsWifiModeActive)
                    _sensorService.Connect($"{WifiHost}:{WifiPort}");
                else if (!string.IsNullOrEmpty(SelectedPort))
                    _sensorService.Connect(SelectedPort);
            }
        }
        UpdateConnectionFlags();
    }
    
    // Simulation Logic
    [ObservableProperty]
    private bool _isSimulating;

    partial void OnIsSimulatingChanged(bool value)
    {
        _sensorService.ToggleSimulation(value);
        // Status updates (including "Connected (Fake)") come from OnStatusChanged event in Service
    }
    
    // ...
    // Removed local _simTimer and _simAngle as Service handles it now.

    public event Action<SensorData>? UpdateGraphAction;

    private void OnDataReceived(SensorData data)
    {
        Console.WriteLine($"[ViewModel] Received: {data.X:F2}");
        
        // Offload to View's Code-behind via event to keep ViewModel clean of UI dependencies
        UpdateGraphAction?.Invoke(data);

        // Record Data
        if (IsMeasuring)
        {
            _currentSamples.Add(new TestSample
            {
                Index = _currentSamples.Count,
                TimestampMs = _currentSamples.Count * 20, // Approx 50Hz = 20ms
                X = data.X,
                Y = data.Y,
                Force1 = data.Force1,
                Force2 = data.Force2,
                Force3 = data.Force3,
                Force4 = data.Force4
            });
        }
    }

    [RelayCommand]
    private void StartStopMeasurement()
    {
        if (IsMeasuring)
        {
            StopMeasurement().ConfigureAwait(false);
        }
        else
        {
            StartMeasurement();
        }
    }

    private void StartMeasurement()
    {
        if (CurrentPatient == null) return;
        
        // Check Connection OR Simulation explicitly as fallback
        if (!_sensorService.IsConnected && !IsSimulating)
        {
             ConnectionStatus = "Vui lòng kết nối cảm biến trước!";
             return;
        }

        IsMeasuring = true;
        _currentSamples.Clear();
        
        // Notify View to clear graph
        UpdateGraphAction?.Invoke(null!); 

        ActionButtonText = "DỪNG ĐO";
    }

    private async Task StopMeasurement()
    {
        IsMeasuring = false;
        ActionButtonText = "ĐANG LƯU...";
        
        try 
        {
            // Create Session
            var session = new TestSession
            {
                PatientId = CurrentPatient!.PatientId,
                TestDate = DateTime.Now,
                MeanX = _currentSamples.Count > 0 ? _currentSamples.Average(s => s.X) : 0,
                MeanY = _currentSamples.Count > 0 ? _currentSamples.Average(s => s.Y) : 0,
                TestSamples = new List<TestSample>(_currentSamples), // Copy buffer
                Notes = $"Lưu lúc {DateTime.Now:HH:mm:ss}"
            };

            await _mainViewModel.SessionService.SaveSessionAsync(session);
            
            ActionButtonText = "ĐO XONG (ĐÃ LƯU)";
            await Task.Delay(2000);
        }
        catch (Exception ex)
        {
            ActionButtonText = "LỖI LƯU!";
            Console.WriteLine(ex);
        }
        finally
        {
            ActionButtonText = "BẮT ĐẦU ĐO";
        }
    }
}
