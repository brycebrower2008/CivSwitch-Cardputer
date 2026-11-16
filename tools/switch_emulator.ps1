param([int]$Port = 9026)
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "CivSwitch fake console / TCP listener" -ForegroundColor Cyan
Write-Host "This is a connectivity emulator only. It cannot emulate an exploit or load a payload."
Write-Host ""
Write-Host "Your PC IPv4 addresses (enter one on the Cardputer):" -ForegroundColor Yellow
try {
    Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
        Format-Table InterfaceAlias, IPAddress -AutoSize | Out-Host
} catch {
    ipconfig | Out-Host
}
Write-Host "Cardputer must be on the same LAN, and firewall must allow inbound TCP $Port." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host ""

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
try {
    $listener.Start()
    Write-Host "Listening on 0.0.0.0:$Port ..." -ForegroundColor Green
    while ($true) {
        $client = $listener.AcceptTcpClient()
        try {
            $remote = $client.Client.RemoteEndPoint
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Connection from $remote" -ForegroundColor Green
            $stream = $client.GetStream()
            $response = [System.Text.Encoding]::ASCII.GetBytes("CIVSWITCH-EMU/1 READY" + [char]13 + [char]10)
            $stream.Write($response, 0, $response.Length)
            $stream.Flush()
            Start-Sleep -Milliseconds 50
        } catch {
            Write-Host "Client error: $_" -ForegroundColor Red
        } finally {
            $client.Close()
        }
    }
} catch {
    Write-Host "Unable to start emulator: $_" -ForegroundColor Red
    Write-Host "Port $Port may be in use, or the firewall may be blocking it." -ForegroundColor Yellow
    exit 1
} finally {
    $listener.Stop()
}
