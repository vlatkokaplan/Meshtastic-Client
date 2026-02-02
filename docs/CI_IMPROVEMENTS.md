# CI Improvements TODO

Suggested optimizations for `.github/workflows/ci.yml`.

## 1. Add Dependency Caching

Add after checkout step for Windows build:

```yaml
- name: Cache vcpkg
  uses: actions/cache@v4
  with:
    path: C:\vcpkg\installed
    key: vcpkg-x64-windows-${{ hashFiles('.github/workflows/ci.yml') }}
    restore-keys: vcpkg-x64-windows-

- name: Cache Qt
  uses: actions/cache@v4
  with:
    path: ${{ runner.temp }}/Qt
    key: qt-6.5.3-windows-msvc2019
```

## 2. Include VC++ Runtime

Users without Visual C++ runtime will get DLL errors.

**Option A** - Bundle the runtime DLLs:
```yaml
# In packaging step
copy "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\14.38.33135\x64\Microsoft.VC143.CRT\*.dll" meshtastic-vibe-client-win\
```

**Option B** - Create an installer (NSIS/Inno Setup) that includes the runtime.

## 3. Add macOS Build

```yaml
build-macos:
  runs-on: macos-latest
  steps:
    - uses: actions/checkout@v4

    - name: Install dependencies
      run: |
        brew install qt@6 protobuf cmake

    - name: Configure
      run: |
        mkdir build
        cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)

    - name: Build
      run: cmake --build build -j$(sysctl -n hw.ncpu)

    - name: Package
      run: |
        cd build
        $(brew --prefix qt@6)/bin/macdeployqt meshtastic-vibe-client.app -dmg

    - name: Upload macOS Artifacts
      if: startsWith(github.ref, 'refs/tags/')
      uses: actions/upload-artifact@v4
      with:
        name: macos-dmg
        path: build/*.dmg
```

Update `create-release` job to include macOS:
```yaml
needs: [build-linux, build-windows, build-macos]
```

## 4. Matrix Build for Multiple Qt Versions (Optional)

```yaml
strategy:
  matrix:
    qt-version: ['6.5.3', '6.6.2']
```

## 5. Build Caching with ccache

```yaml
# Linux build - add ccache
- name: Setup ccache
  uses: hendrikmuhs/ccache-action@v1
  with:
    key: linux-build

- name: Configure
  run: |
    cmake -B build -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

## 6. Cancel Outdated Builds

Add at top level of workflow:
```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

## 7. Add Version Info to Windows Executable

Create `resources/version.rc.in`:
```rc
1 VERSIONINFO
FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904E4"
        BEGIN
            VALUE "ProductName", "@PROJECT_DISPLAY_NAME@"
            VALUE "FileVersion", "@PROJECT_VERSION@"
            VALUE "ProductVersion", "@PROJECT_VERSION@"
        END
    END
END
```

Add to `CMakeLists.txt`:
```cmake
if(WIN32)
  configure_file(${CMAKE_SOURCE_DIR}/resources/version.rc.in ${CMAKE_BINARY_DIR}/version.rc)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/version.rc)
endif()
```

## Impact Summary

| Improvement | Estimated Savings |
|-------------|-------------------|
| vcpkg caching | ~3-5 min per build |
| Qt caching | ~2-3 min per build |
| ccache | ~50% faster rebuilds |
| Cancel outdated builds | Saves CI minutes |
| macOS build | Wider audience |
| VC++ runtime | Fewer user issues |
