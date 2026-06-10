<#
.SYNOPSIS
    SIBA build helper para Windows — equivalente ao Makefile (Linux).

.DESCRIPTION
    Espelha os alvos do Makefile: build / flash / clean / menuconfig / debug.
    Pressupõe o setup padrão do Zephyr no Windows (getting started guide):
      - workspace west em  %USERPROFILE%\zephyrproject  com venv em .venv
      - OpenOCD da WCH via PlatformIO em %USERPROFILE%\.platformio\packages\tool-openocd-riscv-wch

    Caminhos diferentes? Ajuste a seção "Caminhos" abaixo ou defina as
    variáveis de ambiente SIBA_ZEPHYR_WORKSPACE / SIBA_WCH_OPENOCD / SIBA_GDB.

.EXAMPLE
    .\make.ps1                  # build (alvo padrão)
    .\make.ps1 flash
    .\make.ps1 clean
    .\make.ps1 menuconfig
    .\make.ps1 build -Board native_sim   # (native_sim NÃO roda no Windows — use WSL)
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'flash', 'clean', 'menuconfig', 'debug', 'native_sim', 'help')]
    [string]$Target = 'build',

    [string]$Board = 'siba_board'
)

$ErrorActionPreference = 'Stop'

# ---------------- Caminhos (edite aqui se o seu setup for diferente) ----------------
$ProjectDir = $PSScriptRoot

$ZephyrWorkspace = if ($env:SIBA_ZEPHYR_WORKSPACE) { $env:SIBA_ZEPHYR_WORKSPACE }
                   else { Join-Path $env:USERPROFILE 'zephyrproject' }
$VenvActivate    = Join-Path $ZephyrWorkspace '.venv\Scripts\Activate.ps1'

$WchOpenocd = if ($env:SIBA_WCH_OPENOCD) { $env:SIBA_WCH_OPENOCD }
              else { Join-Path $env:USERPROFILE '.platformio\packages\tool-openocd-riscv-wch\bin\openocd.exe' }
$WchCfg     = Join-Path (Split-Path $WchOpenocd -Parent) 'wch-riscv.cfg'

$Gdb = if ($env:SIBA_GDB) { $env:SIBA_GDB }
       else { Join-Path $env:USERPROFILE 'zephyr-sdk-1.0.1\gnu\riscv64-zephyr-elf\bin\riscv64-zephyr-elf-gdb.exe' }

$BuildDir = Join-Path $ZephyrWorkspace 'build'
$Elf      = Join-Path $BuildDir 'zephyr\zephyr.elf'
$Bin      = Join-Path $BuildDir 'zephyr\zephyr.bin'
# -------------------------------------------------------------------------------------

function Invoke-West {
    param([string[]]$WestArgs)

    if (-not (Test-Path $VenvActivate)) {
        throw "venv do Zephyr nao encontrada em '$VenvActivate'. Ajuste SIBA_ZEPHYR_WORKSPACE ou a secao Caminhos do make.ps1."
    }
    Push-Location $ZephyrWorkspace
    try {
        & $VenvActivate
        & west @WestArgs
        if ($LASTEXITCODE -ne 0) { throw "west falhou (exit code $LASTEXITCODE)" }
    }
    finally {
        Pop-Location
    }
}

function Assert-Openocd {
    if (-not (Test-Path $WchOpenocd)) {
        throw "OpenOCD da WCH nao encontrado em '$WchOpenocd'. Instale via PlatformIO (tool-openocd-riscv-wch) ou defina SIBA_WCH_OPENOCD."
    }
}

switch ($Target) {
    'build' {
        Invoke-West @('build', '-b', $Board, $ProjectDir, '--', "-DBOARD_ROOT=$ProjectDir")
    }

    'clean' {
        Invoke-West @('build', '-b', $Board, $ProjectDir, '--pristine', '--', "-DBOARD_ROOT=$ProjectDir")
    }

    'menuconfig' {
        # O menuconfig TUI precisa do pacote python "windows-curses" na venv
        # (pip install windows-curses). Alternativa grafica: trocar por guiconfig.
        Invoke-West @('build', '-b', $Board, $ProjectDir, '-t', 'menuconfig', '--', "-DBOARD_ROOT=$ProjectDir")
    }

    'flash' {
        Assert-Openocd
        if (-not (Test-Path $Bin)) { throw "Firmware nao encontrado em '$Bin'. Rode '.\make.ps1 build' antes." }
        # OpenOCD interpreta '\' como escape — caminho precisa de '/'
        $BinFwd = $Bin -replace '\\', '/'
        & $WchOpenocd -f $WchCfg `
            -c 'init' `
            -c 'halt' `
            -c "program $BinFwd 0x00000000 verify reset" `
            -c 'shutdown'
        if ($LASTEXITCODE -ne 0) { throw "openocd falhou (exit code $LASTEXITCODE)" }
    }

    'debug' {
        Assert-Openocd
        if (-not (Test-Path $Gdb)) { throw "GDB nao encontrado em '$Gdb'. Defina SIBA_GDB apontando para o riscv64-zephyr-elf-gdb.exe do SDK." }
        Write-Host 'Iniciando servidor de debug OpenOCD...'
        $ocd = Start-Process -FilePath $WchOpenocd -ArgumentList @('-f', $WchCfg) -PassThru -WindowStyle Hidden
        try {
            Write-Host "Aguardando OpenOCD (PID: $($ocd.Id))..."
            Start-Sleep -Seconds 2
            & $Gdb $Elf `
                -ex 'target extended-remote :3333' `
                -ex 'monitor halt' `
                -ex 'load' `
                -ex 'monitor reset halt'
        }
        finally {
            if (-not $ocd.HasExited) { Stop-Process -Id $ocd.Id -Force }
        }
    }

    'native_sim' {
        Write-Host 'O modo mock (native_sim) usa CONFIG_ARCH_POSIX e so compila/roda em Linux.' -ForegroundColor Yellow
        Write-Host 'No Windows, use o WSL: dentro dele, "make native_sim_run" funciona normalmente.' -ForegroundColor Yellow
        exit 1
    }

    'help' {
        Get-Help $PSCommandPath -Detailed
    }
}
