[CmdletBinding()]
param(
    [ValidateSet('Release', 'Release-AVX', 'VerifiedDX11ASAN')]
    [string[]]$Configuration = @(
        'Release',
        'Release-AVX',
        'VerifiedDX11ASAN'
    ),
    [ValidateSet('text', 'json')]
    [string]$Format = 'text',
    [ValidateRange(1, 1000000)]
    [uint32]$Iterations = 20000,
    [uint64]$Seed = 0x5356504150497633,
    [switch]$NoBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..')
)
$solution = Join-Path $repoRoot 'src\svp-tests.sln'
$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products * `
    -requires Microsoft.Component.MSBuild -property installationPath
if (-not $install) {
    throw 'Visual Studio with MSBuild was not found'
}
$msbuild = Join-Path $install 'MSBuild\Current\Bin\amd64\MSBuild.exe'

function Invoke-Client {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [Parameter(Mandatory)]
        [int[]]$ExpectedExit
    )

    Write-Host "svp runner case=$Name expected_exit=$($ExpectedExit -join ',')"
    & $Path @Arguments
    $code = $LASTEXITCODE
    if ($ExpectedExit -notcontains $code) {
        throw "$Path exited with $code"
    }
}

$secondSeed = $Seed -bxor [uint64]11400714819323198485
foreach ($item in $Configuration) {
    if (-not $NoBuild) {
        & $msbuild $solution "/p:Configuration=$item" `
            '/p:Platform=x64' '/m' '/nologo' '/v:minimal'
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed for $item"
        }
    }

    $client = Join-Path $repoRoot `
        "_build\svp-tests\x64\$item\svp-test-client.exe"
    if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
        throw "Missing test client $client"
    }

    $savedPath = $env:Path
    $savedAsan = $env:ASAN_OPTIONS
    try {
        if ($item -eq 'VerifiedDX11ASAN') {
            $asanRuntime = Get-ChildItem `
                -LiteralPath (Join-Path $install 'VC\Tools\MSVC') `
                -Recurse -Filter 'clang_rt.asan_dynamic-x86_64.dll' |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if (-not $asanRuntime) {
                throw 'AddressSanitizer runtime was not found'
            }
            $env:Path = $asanRuntime.DirectoryName + ';' + $env:Path
            $env:ASAN_OPTIONS = `
                'symbolize=1:halt_on_error=1:abort_on_error=1'
        }

        Invoke-Client -Name 'help' -Path $client -Arguments @('--help') `
            -ExpectedExit @(0)
        Write-Host 'svp runner case=json-contract expected_exit=0'
        $jsonText = & $client @(
            '--suite', 'schema',
            '--repo', $repoRoot,
            '--iterations', '1',
            '--format', 'json'
        )
        if ($LASTEXITCODE -ne 0) {
            throw "JSON contract run exited with $LASTEXITCODE"
        }
        $jsonResult = $jsonText | ConvertFrom-Json
        if ($jsonResult.ok -ne $true -or $jsonResult.tests -ne 3 -or
            $jsonResult.passed -ne 3 -or $jsonResult.assertions -lt 100 -or
            $jsonResult.rejections -ne 2 -or
            $jsonResult.failures.Count -ne 0) {
            throw 'JSON contract output is invalid'
        }
        Write-Host $jsonText
        $invalidCases = @(
            @{
                Name = 'unknown-suite'
                Arguments = @('--suite', 'invalid')
            },
            @{
                Name = 'zero-iterations'
                Arguments = @('--iterations', '0')
            },
            @{
                Name = 'negative-iterations'
                Arguments = @('--iterations', '-1')
            },
            @{
                Name = 'overflow-iterations'
                Arguments = @('--iterations', '4294967296')
            },
            @{
                Name = 'invalid-seed'
                Arguments = @('--seed', 'invalid')
            },
            @{
                Name = 'unknown-format'
                Arguments = @('--format', 'yaml')
            },
            @{
                Name = 'missing-value'
                Arguments = @('--suite')
            },
            @{
                Name = 'unknown-argument'
                Arguments = @('--invalid')
            }
        )
        foreach ($case in $invalidCases) {
            Invoke-Client -Name $case.Name -Path $client `
                -Arguments $case.Arguments -ExpectedExit @(2)
        }
        Invoke-Client -Name 'missing-repository' -Path $client -Arguments @(
            '--suite', 'optic-api',
            '--repo', (Join-Path $repoRoot 'tests\svp\missing-repository'),
            '--iterations', '1'
        ) -ExpectedExit @(1)
        foreach ($runSeed in @($Seed, $secondSeed)) {
            Invoke-Client -Name "full-$runSeed" -Path $client -Arguments @(
                '--repo', $repoRoot,
                '--iterations', [string]$Iterations,
                '--seed', [string]$runSeed,
                '--format', $Format
            ) -ExpectedExit @(0)
        }
    }
    finally {
        $env:Path = $savedPath
        $env:ASAN_OPTIONS = $savedAsan
    }
}
