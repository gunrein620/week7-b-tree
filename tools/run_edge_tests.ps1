param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Tests
)

$passed = 0
$failed = 0

foreach ($test in $Tests) {
    & $test
    if ($LASTEXITCODE -eq 0) {
        $passed++
    } else {
        $failed++
    }
}

Write-Output "Edge test executables: $passed passed, $failed failed"
exit $failed
