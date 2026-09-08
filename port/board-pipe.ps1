# Raw COM <-> stdin/stdout pipe, the Windows half of the WSL bridge.
#
# The board is not attached to this machine: its CH340 USB-TTL adapter arrives
# over the network through USB Network Gate, so it exists here only as a COM
# port. WSL cannot see the USB device at all -- usbipd's stub is incompatible
# with the UNG filter driver (fusbhub.sys), and stacking a second USB-over-
# network layer on the first would be silly anyway. So this moves the bytes
# rather than the device: socat inside WSL runs this script and gets a pty that
# behaves like the /dev/cu.usbserial-110 the board answers on from macOS.
#
# Not meant to be run by hand -- see board-console.sh (WSL) or
# board-console.ps1 (Windows terminal).
#
# Why the polling loop and not two CopyToAsync calls: when a Windows process is
# launched from WSL, its stdin is not a plain Win32 pipe. A read with no data
# waiting returns 0 immediately instead of blocking, and .NET reports that as
# end-of-stream -- so a straight copy loop tears the bridge down about a second
# after it starts. Reading both sides as tasks and treating a 0-byte stdin read
# as "nothing yet" keeps the pipe up.

[CmdletBinding()]
param(
	[string] $Port  = 'COM3',
	[int]    $Baud  = 1500000,
	[switch] $NoDtr
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.Handshake       = 'None'
$sp.DtrEnable       = -not $NoDtr
$sp.RtsEnable       = -not $NoDtr
$sp.ReadBufferSize  = 1MB
$sp.WriteBufferSize = 64KB

try {
	$sp.Open()
	$stdin  = [Console]::OpenStandardInput()
	$stdout = [Console]::OpenStandardOutput()

	$rbuf = New-Object byte[] 65536
	$sbuf = New-Object byte[] 4096
	$fromBoard = $sp.BaseStream.ReadAsync($rbuf, 0, $rbuf.Length)
	$toBoard   = $stdin.ReadAsync($sbuf, 0, $sbuf.Length)

	while ($true) {
		$which = [System.Threading.Tasks.Task]::WaitAny(@($fromBoard, $toBoard), 200)

		if ($which -eq 0) {
			$n = $fromBoard.Result
			if ($n -gt 0) {
				$stdout.Write($rbuf, 0, $n)
				$stdout.Flush()
			}
			$fromBoard = $sp.BaseStream.ReadAsync($rbuf, 0, $rbuf.Length)
		}
		elseif ($which -eq 1) {
			$n = $toBoard.Result
			if ($n -gt 0) {
				$sp.BaseStream.Write($sbuf, 0, $n)
				$sp.BaseStream.Flush()
			} else {
				# Not EOF here, just an empty poll -- do not spin the CPU.
				Start-Sleep -Milliseconds 20
			}
			$toBoard = $stdin.ReadAsync($sbuf, 0, $sbuf.Length)
		}
		# $which -eq -1 is the 200 ms timeout: nothing to do, loop again.
	}
}
finally {
	if ($sp.IsOpen) { $sp.Close() }
	$sp.Dispose()
}
