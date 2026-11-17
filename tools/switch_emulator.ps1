param([int]$Port = 9026)
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "CivSwitch PC TCP emulator / payload receiver" -ForegroundColor Cyan
Write-Host "SIMULATION ONLY. This is NOT a Nintendo Switch, loader, or exploit."
Write-Host ""
Write-Host "PC IPv4 addresses (enter the LAN address on Cardputer):" -ForegroundColor Yellow
try {
    Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
        Format-Table InterfaceAlias, IPAddress -AutoSize | Out-Host
} catch { ipconfig | Out-Host }
Write-Host "Same Wi-Fi/LAN required; inbound TCP $Port permitted by Windows Firewall." -ForegroundColor Yellow
Write-Host "Cardputer C checks reachability, P sends JS from the microSD for TESTING." -ForegroundColor Yellow
Write-Host "Nothing is evaluated or installed. Ctrl+C to exit." -ForegroundColor Yellow
Write-Host ""

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
try {
    $listener.Start()
    Write-Host "Listening on 0.0.0.0:$Port..." -ForegroundColor Green
    while ($true) {
        $client = $listener.AcceptTcpClient()
        $memory = $null
        try {
            $client.ReceiveTimeout = 15000
            $remote = $client.Client.RemoteEndPoint
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Connection from $remote" -ForegroundColor Green
            $stream = $client.GetStream()
            $stream.ReadTimeout = 15000
            $banner = [System.Text.Encoding]::ASCII.GetBytes("CIVSWITCH-EMU/1 READY" + [char]13 + [char]10)
            $stream.Write($banner, 0, $banner.Length)
            $stream.Flush()
            $memory = New-Object System.IO.MemoryStream
            $buffer = New-Object byte[] 8192
            $truncated = $false
            try {
                while ($true) {
                    $received = $stream.Read($buffer, 0, $buffer.Length)
                    if ($received -le 0) { break }
                    if ($memory.Length + $received -le 500000) {
                        $memory.Write($buffer, 0, $received)
                    } else {
                        $truncated = $true
                        break
                    }
                }
            } catch [System.IO.IOException] {
                if ($memory.Length -gt 0) {
                    Write-Host "Read timed out (incomplete transfer)." -ForegroundColor Yellow
                }
            }
            if ($memory.Length -gt 0) {
                $sha = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $hash = [System.BitConverter]::ToString($sha.ComputeHash($memory.ToArray())).Replace("-", "").ToLowerInvariant()
                } finally { $sha.Dispose() }
                Write-Host "Received $($memory.Length) JS bytes. SHA256: $hash" -ForegroundColor Green
                if ($truncated) { Write-Host "PAYLOAD TOO LARGE / TRUNCATED" -ForegroundColor Red }
                $reply = [System.Text.Encoding]::ASCII.GetBytes("CIVSWITCH-EMU/1 PAYLOAD RECEIVED (NOT EXECUTED)" + [char]13 + [char]10)
                $stream.Write($reply, 0, $reply.Length)
                $stream.Flush()
            } else {
                Write-Host "TCP probe only: no data sent."
            }
        } catch {
            Write-Host "Client error: $_" -ForegroundColor Red
        } finally {
            if ($memory) { $memory.Dispose() }
            $client.Close()
        }
    }
} catch {
    Write-Host "Unable to start listener: $_" -ForegroundColor Red
    exit 1
} finally {
    $listener.Stop()
}
