@echo off
setlocal enabledelayedexpansion

set "NPOINTS=%~1"
if "%NPOINTS%"=="" set "NPOINTS=10000"
set "RUNS=%~2"
if "%RUNS%"=="" set "RUNS=10"

echo ==================================================
echo  Automated Performance Benchmark
echo ==================================================
echo  Target points:      %NPOINTS%
echo  Runs per config:    %RUNS%
echo ==================================================
echo.

set "THREADS=1 2 4 8"

for %%N in (%THREADS%) do (
    echo --- Threads: %%N ---
    set /a sum_us=0
    set /a min_us=2147483647
    set /a max_us=0
    for /l %%i in (1,1,%RUNS%) do (
        for /f "tokens=2" %%t in ('mandelbrot.exe %%N %NPOINTS%') do (
            set /a sum_us+=%%t
            if %%t LSS !min_us! set /a min_us=%%t
            if %%t GTR !max_us! set /a max_us=%%t
            set /a sec=%%t/1000000
            set /a usrem=%%t-sec*1000000
            set /a ms3=usrem/1000
            echo   [%%i/%RUNS%]  time: !sec!.!ms3! s
        )
    )
    set /a avg_us=sum_us/RUNS
    set /a avg_%%N=avg_us
    set /a min_%%N=min_us
    set /a max_%%N=max_us
    set /a as=avg_us/1000000
    set /a ar=avg_us-as*1000000
    set /a am=ar/1000
    set /a ms_=min_us/1000000
    set /a mr=min_us-ms_*1000000
    set /a mm=mr/1000
    set /a xs=max_us/1000000
    set /a xr=max_us-xs*1000000
    set /a xm=xr/1000
    echo   Minimum:  !ms_!.!mm! s
    echo   Average:  !as!.!am! s
    echo   Maximum:  !xs!.!xm! s
    echo.
)

echo ==================================================
echo  Summary
echo ==================================================
echo.
echo  Threads   Average(s)   Speedup   Efficiency
echo  -------   ----------   -------   ----------

set /a base=avg_1

for %%N in (%THREADS%) do (
    set /a cur=avg_%%N
    set /a q=base/cur
    set /a r=base-q*cur
    set /a sp100=q*100 + r*100/cur
    set /a sp_int=sp100/100
    set /a sp_dec=sp100-sp_int*100
    set /a eff=sp100/%%N
    set /a cs=cur/1000000
    set /a cr=cur-cs*1000000
    set /a cm=cr/1000
    echo  %%N         !cs!.!cm!         !sp_int!.!sp_dec!       !eff!%%
)

endlocal