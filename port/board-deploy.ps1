# Положить свежую сборку MEECHO на BIGTREETECH CB2 по ssh.
#
# Плата стоит не здесь; пока на ней вендорский Linux с карты, к ней есть ssh,
# и это самый быстрый способ доставить туда ядро с загрузочным архивом: они
# кладутся прямо в /boot, а «эту загрузку - MEECHO» выбирается файлом-флагом,
# который U-Boot снимает перед запуском (см. cb2-fixup.cmd).
#
# ВНИМАНИЕ. Загрузка MEECHO отрезает и ssh, и последовательную консоль: обе
# идут через Linux на карте (консоль - потому что USB-TTL раздаётся с самой
# платы через USB Network Gate). То есть -Boot делает попытку ОДНОРАЗОВОЙ:
# вернуть плату сможет только человек, выключив и включив питание. Пока это
# так, всё, что можно проверить стендом, надо проверять стендом.
#
#   board-deploy.ps1                  только скопировать
#   board-deploy.ps1 -Boot            скопировать, взвести флаг, перезагрузить
#   board-deploy.ps1 -Boot -RootEmmc  то же, но корень с eMMC, а не ramdisk
#   board-deploy.ps1 -Status          что сейчас лежит на плате
[CmdletBinding()]
param(
	[switch]$Boot,
	[switch]$RootEmmc,
	[switch]$Status,
	[string]$BoardHost = $(if ($env:MEECHO_BOARD) { $env:MEECHO_BOARD } else { 'root@cb2' }),
	[int]$Port = $(if ($env:MEECHO_BOARD_PORT) { [int]$env:MEECHO_BOARD_PORT } else { 22 }),
	[string]$Key = "$env:USERPROFILE\.ssh\<ключ>",
	# Каталог со сборкой. По умолчанию комплект, который делает mkcard.sh.
	[string]$Kit = 'D:\minix\port\cb2-card'
)

$ErrorActionPreference = 'Stop'

$sshArgs = @('-o', 'BatchMode=yes', '-i', $Key, '-o', 'IdentitiesOnly=yes',
	'-o', 'ConnectTimeout=15')

function Invoke-Board([string]$Command) {
	& ssh @sshArgs $BoardHost -p $Port $Command
	if ($LASTEXITCODE -ne 0) { throw "ssh вернул $LASTEXITCODE" }
}

if ($Status) {
	Invoke-Board 'uname -sr; echo ---; ls -l /boot/meecho/ /boot/fixup.scr /boot/meecho.go 2>&1; echo ---; md5sum /boot/meecho/* 2>/dev/null'
	return
}

foreach ($f in @('meecho\kernel.bin', 'meecho\boot.mba', 'fixup.scr')) {
	if (-not (Test-Path (Join-Path $Kit $f))) {
		throw "нет $Kit\$f - сначала mkcard.sh"
	}
}

Write-Host 'Копирую в /tmp платы...'
& scp @sshArgs '-P' $Port `
	(Join-Path $Kit 'meecho\kernel.bin') `
	(Join-Path $Kit 'meecho\boot.mba') `
	(Join-Path $Kit 'fixup.scr') `
	"${BoardHost}:/tmp/"
if ($LASTEXITCODE -ne 0) { throw "scp вернул $LASTEXITCODE" }

# Вендорский boot.scr сохраняется один раз и не перезаписывается: он и есть
# путь назад к заводской загрузке, если fixup.scr придётся убрать.
Invoke-Board 'mkdir -p /boot/meecho; cp -n /boot/boot.scr /root/boot.scr.vendor-backup; mv /tmp/kernel.bin /tmp/boot.mba /boot/meecho/ && mv /tmp/fixup.scr /boot/fixup.scr && sync && md5sum /boot/meecho/*'

if ($RootEmmc) {
	Invoke-Board 'touch /boot/meecho/root_emmc; sync; echo "root_emmc: on"'
} else {
	Invoke-Board 'rm -f /boot/meecho/root_emmc; sync; echo "root_emmc: off"'
}

if (-not $Boot) {
	Write-Host 'Скопировано. Загрузку не трогал: -Boot, когда будете готовы.'
	return
}

Write-Host ''
Write-Host 'ВНИМАНИЕ: после перезагрузки плата уйдёт в MEECHO, и ssh с' -ForegroundColor Yellow
Write-Host 'консолью пропадут до тех пор, пока человек не передёрнет питание.' -ForegroundColor Yellow
Write-Host ''

Invoke-Board 'touch /boot/meecho.go && sync && ls -l /boot/meecho.go && (nohup sh -c "sleep 1; reboot" >/dev/null 2>&1 &) ; echo REBOOTING'
Write-Host 'Флаг взведён, плата перезагружается. Смотреть консоль.'
