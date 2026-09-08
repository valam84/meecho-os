# Serial console for the BIGTREETECH CB2 board.
#
# The board is not attached to this machine: its CH340 USB-TTL adapter arrives
# over the network through USB Network Gate, and shows up here as a COM port.
# CB2 puts its console on uart2 at 1500000 8N1, no flow control -- the same
# settings that work as `picocom -b 1500000 --databits 8 --parity n
# --stopbits 1 --flow n` elsewhere.
#
# Modes:
#   .\board-console.ps1                     interactive terminal, Ctrl+] to quit
#   .\board-console.ps1 -Listen 30          just watch for 30 s (boot log)
#   .\board-console.ps1 -Send 'uname -a'    send line(s), then listen -Wait s
#   .\board-console.ps1 -Log boot.log       tee everything into a file
# Add -Timestamp to prefix every logged line with elapsed seconds.

[CmdletBinding()]
param(
	[string]   $Port      = 'COM3',
	[int]      $Baud      = 1500000,
	[string[]] $Send      = @(),
	[double]   $Wait      = 5,
	[double]   $Listen    = 0,
	[string]   $Log       = '',
	[switch]   $Timestamp,
	[switch]   $NoDtr,
	[switch]   $Poke
)

$ErrorActionPreference = 'Stop'

if ($Port -notin [System.IO.Ports.SerialPort]::GetPortNames()) {
	Write-Warning "$Port is not in the port list: $([System.IO.Ports.SerialPort]::GetPortNames() -join ', ')"
	Write-Warning "If the board is remote, check that USB Network Gate still has the CH340 connected."
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.Handshake   = 'None'
$sp.DtrEnable   = -not $NoDtr
$sp.RtsEnable   = -not $NoDtr
$sp.ReadTimeout = 100
# 1500000 baud is ~150 KB/s; the default 4 KB input buffer overruns on a boot log.
$sp.ReadBufferSize  = 1MB
$sp.WriteBufferSize = 64KB

$logStream = $null
if ($Log) {
	$logStream = [System.IO.StreamWriter]::new($Log, $true, [System.Text.Encoding]::UTF8)
	$logStream.AutoFlush = $true
}
$started  = Get-Date
$atLineStart = $true

function Write-Chunk([string]$text) {
	if (-not $text) { return }
	[Console]::Write($text)
	if ($script:logStream) {
		if ($Timestamp) {
			# Prefix each line with seconds since start, the way QEMU run logs read.
			foreach ($ch in $text.ToCharArray()) {
				if ($script:atLineStart -and $ch -ne "`n") {
					$script:logStream.Write([string]::Format([cultureinfo]::InvariantCulture,
						'[{0,8:F3}] ', ((Get-Date) - $script:started).TotalSeconds))
					$script:atLineStart = $false
				}
				$script:logStream.Write($ch)
				if ($ch -eq "`n") { $script:atLineStart = $true }
			}
		} else {
			$script:logStream.Write($text)
		}
	}
}

function Drain([double]$seconds) {
	$buf = New-Object byte[] 65536
	$end = (Get-Date).AddSeconds($seconds)
	$nextPoke = (Get-Date).AddMilliseconds(300)
	while ((Get-Date) -lt $end) {
		try {
			$n = $sp.Read($buf, 0, $buf.Length)
			if ($n -gt 0) {
				Write-Chunk ([System.Text.Encoding]::UTF8.GetString($buf, 0, $n))
				# Keep reading while data flows: a boot log must not be cut short.
				$end = (Get-Date).AddSeconds($seconds)
			}
		} catch [TimeoutException] { }

		# -Poke answers U-Boot's "press ENTER" after a baud rate change. It
		# cannot be timed by hand: the prompt appears mid-boot and the loader
		# waits there forever. Sent blind and repeatedly, which is harmless -
		# this loader's autoboot cannot be interrupted by a keypress anyway.
		if ($Poke -and (Get-Date) -ge $nextPoke) {
			$sp.Write("`r")
			$nextPoke = (Get-Date).AddMilliseconds(300)
		}
	}
}

try {
	$sp.Open()
	Write-Host "[$Port @ $Baud 8N1, flow none] connected" -ForegroundColor DarkGray

	if ($Send.Count -gt 0) {
		foreach ($line in $Send) {
			$sp.Write($line + "`r")
			Start-Sleep -Milliseconds 150
		}
		Drain $Wait
	}
	elseif ($Listen -gt 0) {
		Drain $Listen
	}
	else {
		Write-Host "[interactive; Ctrl+] to quit]" -ForegroundColor DarkGray
		$buf = New-Object byte[] 65536
		while ($true) {
			try {
				$n = $sp.Read($buf, 0, $buf.Length)
				if ($n -gt 0) { Write-Chunk ([System.Text.Encoding]::UTF8.GetString($buf, 0, $n)) }
			} catch [TimeoutException] { }

			while ([Console]::KeyAvailable) {
				$k = [Console]::ReadKey($true)
				# Ctrl+] -- the telnet escape, and not a key the board needs.
				if ($k.Modifiers -band [ConsoleModifiers]::Control -and $k.Key -eq 'Oem6') { return }
				if ($k.Key -eq 'Enter') { $sp.Write("`r") }
				elseif ($k.KeyChar) { $sp.Write([string]$k.KeyChar) }
			}
		}
	}
}
finally {
	if ($sp.IsOpen) { $sp.Close() }
	$sp.Dispose()
	if ($logStream) { $logStream.Dispose(); Write-Host "`n[log written to $Log]" -ForegroundColor DarkGray }
}
