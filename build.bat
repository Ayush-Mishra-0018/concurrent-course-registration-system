@echo off
echo Compiling Academia Course Registration Portal for Windows

REM Create data directory
mkdir data 2>nul

REM Clean previous build files
echo Cleaning previous builds...
del /F /Q server.exe client.exe 2>nul
del /F /Q *.o controllers\*.o utils\*.o 2>nul

REM Compile server
echo Compiling server...
gcc -Wall -pthread -o server.exe server.c common_utils.c ^
    utils/file_ops.c utils/file_ops2.c utils/file_ops3.c utils/file_ops4.c ^
    controllers/admin_controller_fixed.c controllers/admin_controller2.c ^
    controllers/faculty_controller_fixed.c controllers/faculty_controller2_fixed.c ^
    controllers/student_controller_fixed.c controllers/student_controller2_fixed.c ^
    -pthread

REM Compile client
echo Compiling client...
gcc -Wall -o client.exe client.c common_utils.c

REM Check compilation status
if exist server.exe (
    if exist client.exe (
        echo Compilation successful!
        echo To run the server: server.exe
        echo To run the client: client.exe
        echo Default admin credentials: username=admin, password=admin123
    ) else (
        echo Compilation of client failed!
    )
) else (
    echo Compilation of server failed!
)

echo Done. 