import os, sys, pprint, re, json, pathlib, hashlib, subprocess, glob

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))

def finish(code):
    global executePath
    os.chdir(executePath)
    sys.exit(code)

def error(text):
    print('[ERROR] ' + text)
    finish(1)

win = (sys.platform == 'win32')
mac = (sys.platform == 'darwin')
win32 = win and (os.environ['Platform'] == 'x86')
win64 = win and (os.environ['Platform'] == 'x64')

if win and not 'COMSPEC' in os.environ:
    error('COMSPEC environment variable is not set.')

if win and not win32 and not win64:
    error('Make sure to run from Native Tools Command Prompt.')

os.chdir(scriptPath + '/../../../..')

dirSep = '\\' if win else '/'
pathSep = ';' if win else ':'
libsLoc = 'Libraries' if not win64 else 'Libraries64'
keysLoc = 'cache_keys'

rootDir = os.getcwd()
libsDir = rootDir + dirSep + libsLoc
thirdPartyDir = rootDir + dirSep + 'ThirdParty'
usedPrefix = libsDir + dirSep + 'local'

optionsList = [
    'skip-release',
    'build-qt5',
    'skip-qt5',
    'build-qt6',
    'skip-qt6',
    'build-stackwalk',
]
options = []
runCommand = []
customRunCommand = False
for arg in sys.argv[1:]:
    if customRunCommand:
        runCommand.append(arg)
    if arg in optionsList:
        options.append(arg)
    elif arg == 'run':
        customRunCommand = True
buildQt5 = not 'skip-qt5' in options if win else 'build-qt5' in options
buildQt6 = 'build-qt6' in options if win else not 'skip-qt6' in options

if not os.path.isdir(libsDir + '/' + keysLoc):
    pathlib.Path(libsDir + '/' + keysLoc).mkdir(parents=True, exist_ok=True)
if not os.path.isdir(thirdPartyDir + '/' + keysLoc):
    pathlib.Path(thirdPartyDir + '/' + keysLoc).mkdir(parents=True, exist_ok=True)

pathPrefixes = [
    'ThirdParty\\Strawberry\\perl\\bin',
    'ThirdParty\\Python39',
    'ThirdParty\\NASM',
    'ThirdParty\\jom',
    'ThirdParty\\cmake\\bin',
    'ThirdParty\\yasm',
    'ThirdParty\\gyp',
    'ThirdParty\\Ninja',
] if win else [
    'ThirdParty/gyp',
    'ThirdParty/yasm',
    'ThirdParty/depot_tools',
]
pathPrefix = ''
for singlePrefix in pathPrefixes:
    pathPrefix = pathPrefix + rootDir + dirSep + singlePrefix + pathSep

environment = {
    'MAKE_THREADS_CNT': '-j8',
    'MACOSX_DEPLOYMENT_TARGET': '10.12',
    'UNGUARDED': '-Werror=unguarded-availability-new',
    'MIN_VER': '-mmacosx-version-min=10.12',
    'USED_PREFIX': usedPrefix,
    'ROOT_DIR': rootDir,
    'LIBS_DIR': libsDir,
    'SPECIAL_TARGET': 'win' if win32 else 'win64' if win64 else 'mac',
    'X8664': 'x86' if win32 else 'x64',
    'WIN32X64': 'Win32' if win32 else 'x64',
    'PATH_PREFIX': pathPrefix,
}
ignoreInCacheForThirdParty = [
    'USED_PREFIX',
    'LIBS_DIR',
    'SPECIAL_TARGET',
    'X8664',
    'WIN32X64',
]

environmentKeyString = ''
envForThirdPartyKeyString = ''
for key in environment:
    part = key + '=' + environment[key] + ';'
    environmentKeyString += part
    if not key in ignoreInCacheForThirdParty:
        envForThirdPartyKeyString += part
environmentKey = hashlib.sha1(environmentKeyString.encode('utf-8')).hexdigest()
envForThirdPartyKey = hashlib.sha1(envForThirdPartyKeyString.encode('utf-8')).hexdigest()

modifiedEnv = os.environ.copy()
for key in environment:
    modifiedEnv[key] = environment[key]

modifiedEnv['PATH'] = environment['PATH_PREFIX'] + modifiedEnv['PATH']

def computeFileHash(path):
    sha1 = hashlib.sha1()
    with open(path, 'rb') as f:
        while True:
            data = f.read(256 * 1024)
            if not data:
                break
            sha1.update(data)
    return sha1.hexdigest()

def computeCacheKey(stage):
    if (stage['location'] == 'ThirdParty'):
        envKey = envForThirdPartyKey
    else:
        envKey = environmentKey
    objects = [
        envKey,
        stage['location'],
        stage['name'],
        stage['version'],
        stage['commands']
    ]
    for pattern in stage['dependencies']:
        pathlist = glob.glob(libsDir + '/' + pattern)
        items = [pattern]
        if len(pathlist) == 0:
            pathlist = glob.glob(thirdPartyDir + '/' + pattern)
        if len(pathlist) == 0:
            error('Nothing found: ' + pattern)
        for path in pathlist:
            if not os.path.exists(path):
                error('Not found: ' + path)
            items.append(computeFileHash(path))
        objects.append(':'.join(items))
    return hashlib.sha1(';'.join(objects).encode('utf-8')).hexdigest()

def keyPath(stage):
    return stage['directory'] + '/' + keysLoc + '/' + stage['name']

def checkCacheKey(stage):
    if not 'key' in stage:
        error('Key not set in stage: ' + stage['name'])
    key = keyPath(stage)
    if not os.path.exists(stage['directory'] + '/' + stage['name']):
        return 'NotFound'
    if not os.path.exists(key):
        return 'Stale'
    with open(key, 'r') as file:
        return 'Good' if (file.read() == stage['key']) else 'Stale'

def clearCacheKey(stage):
    key = keyPath(stage)
    if os.path.exists(key):
        os.remove(key)

def writeCacheKey(stage):
    if not 'key' in stage:
        error('Key not set in stage: ' + stage['name'])
    key = keyPath(stage)
    with open(key, 'w') as file:
        file.write(stage['key'])

stages = []

def removeDir(folder):
    if win:
        return 'if exist ' + folder + ' rmdir /Q /S ' + folder + '\nif exist ' + folder + ' exit /b 1'
    return 'rm -rf ' + folder

def filterByPlatform(commands):
    commands = commands.split('\n')
    result = ''
    dependencies = []
    version = '0'
    skip = False
    for command in commands:
        m = re.match(r'(!?)([a-z0-9_]+):', command)
        if m and m.group(2) != 'depends' and m.group(2) != 'version':
            scopes = m.group(2).split('_')
            inscope = 'common' in scopes
            if win and 'win' in scopes:
                inscope = True
            if win32 and 'win32' in scopes:
                inscope = True
            if win64 and 'win64' in scopes:
                inscope = True
            if mac and 'mac' in scopes:
                inscope = True
            # if linux and 'linux' in scopes:
            #     inscope = True
            if 'release' in scopes:
                if 'skip-release' in options:
                    inscope = False
                elif len(scopes) == 1:
                    continue
            skip = inscope if m.group(1) == '!' else not inscope
        elif not skip and not re.match(r'\s*#', command):
            if m and m.group(2) == 'version':
                version = version + '.' + command[len(m.group(0)):].strip()
            elif m and m.group(2) == 'depends':
                pattern = command[len(m.group(0)):].strip()
                dependencies.append(pattern)
            else:
                command = command.strip()
                if len(command) > 0:
                    result = result + command + '\n'
    return [result, dependencies, version]

def stage(name, commands, location = 'Libraries'):
    if location == 'Libraries':
        directory = libsDir
    elif location == 'ThirdParty':
        directory = thirdPartyDir
    else:
        error('Unknown location: ' + location)
    [commands, dependencies, version] = filterByPlatform(commands)
    if len(commands) > 0:
        stages.append({
            'name': name,
            'location': location,
            'directory': directory,
            'commands': commands,
            'version': version,
            'dependencies': dependencies
        })

def winFailOnEach(command):
    commands = command.split('\n')
    result = ''
    startingCommand = True
    for command in commands:
        command = re.sub(r'\$([A-Za-z0-9_]+)', r'%\1%', command)
        if re.search(r'\$', command):
            error('Bad command: ' + command)
        appendCall = startingCommand and not re.match(r'(if|for) ', command)
        called = 'call ' + command if appendCall else command
        result = result + called
        if command.endswith('^'):
            startingCommand = False
        else:
            startingCommand = True
            result = result + '\r\nif %errorlevel% neq 0 exit /b %errorlevel%\r\n'
    return result

def printCommands(commands):
    print('---------------------------------COMMANDS-LIST----------------------------------')
    print(commands, end='')
    print('--------------------------------------------------------------------------------')

def run(commands):
    printCommands(commands)
    if win:
        if os.path.exists("command.bat"):
            os.remove("command.bat")
        with open("command.bat", 'w') as file:
            file.write('@echo OFF\r\n' + winFailOnEach(commands))
        result = subprocess.run("command.bat", shell=True, env=modifiedEnv).returncode == 0
        if result and os.path.exists("command.bat"):
            os.remove("command.bat")
        return result
    elif re.search(r'\%', commands):
        error('Bad command: ' + commands)
    else:
        return subprocess.run("set -e\n" + commands, shell=True, env=modifiedEnv).returncode == 0

# Thanks https://stackoverflow.com/a/510364
class _Getch:
    """Gets a single character from standard input.  Does not echo to the
screen."""
    def __init__(self):
        try:
            self.impl = _GetchWindows()
        except ImportError:
            self.impl = _GetchUnix()

    def __call__(self): return self.impl()

class _GetchUnix:
    def __init__(self):
        import tty, sys

    def __call__(self):
        import sys, tty, termios
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch

class _GetchWindows:
    def __init__(self):
        import msvcrt

    def __call__(self):
        import msvcrt
        return msvcrt.getch().decode('ascii')

getch = _Getch()

def runStages():
    onlyStages = []
    rebuildStale = False
    for arg in sys.argv[1:]:
        if arg in options:
            continue
        elif arg == 'silent':
            rebuildStale = True
            continue
        found = False
        for stage in stages:
            if stage['name'] == arg:
                onlyStages.append(arg)
                found = True
                break
        if not found:
            error('Unknown argument: ' + arg)
    count = len(stages)
    index = 0
    for stage in stages:
        if len(onlyStages) > 0 and not stage['name'] in onlyStages:
            continue
        index = index + 1
        version = ('#' + str(stage['version'])) if (stage['version'] != '0') else ''
        prefix = '[' + str(index) + '/' + str(count) + '](' + stage['location'] + '/' + stage['name'] + version + ')'
        print(prefix + ': ', end = '', flush=True)
        stage['key'] = computeCacheKey(stage)
        commands = removeDir(stage['name']) + '\n' + stage['commands']
        checkResult = 'Forced' if len(onlyStages) > 0 else checkCacheKey(stage)
        if checkResult == 'Good':
            print('SKIPPING')
            continue
        elif checkResult == 'NotFound':
            print('NOT FOUND, ', end='')
        elif checkResult == 'Stale' or checkResult == 'Forced':
            if checkResult == 'Stale':
                print('CHANGED, ', end='')
            if rebuildStale:
                checkResult == 'Rebuild'
            else:
                print('(r)ebuild, rebuild (a)ll, (s)kip, (p)rint, (q)uit?: ', end='', flush=True)
                while True:
                    ch = 'r' if rebuildStale else getch()
                    if ch == 'q':
                        finish(0)
                    elif ch == 'p':
                        printCommands(commands)
                        checkResult = 'Printed'
                        break
                    elif ch == 's':
                        checkResult = 'Skip'
                        break
                    elif ch == 'r':
                        checkResult = 'Rebuild'
                        break
                    elif ch == 'a':
                        checkResult = 'Rebuild'
                        rebuildStale = True
                        break
        if checkResult == 'Printed':
            continue
        if checkResult == 'Skip':
            print('SKIPPING')
            continue
        clearCacheKey(stage)
        print('BUILDING:')
        os.chdir(stage['directory'])
        if not run(commands):
            print(prefix + ': FAILED')
            finish(1)
        writeCacheKey(stage)

if customRunCommand:
    os.chdir(executePath)
    command = ' '.join(runCommand) + '\n'
    if not run(command):
        print('FAILED :(')
        finish(1)
    finish(0)

stage('patches', """
    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout 4c21dfa0db
""")

stage('depot_tools', """
mac:
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
""", 'ThirdParty')

if not mac or 'build-stackwalk' in options:
    stage('gyp', """
win:
    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout d6c5dd51dc
depends:patches/gyp.diff
    git apply $LIBS_DIR/patches/gyp.diff
mac:
    python3 -m pip install --ignore-installed git+https://github.com/desktop-app/gyp-next@main
    mkdir gyp
""", 'ThirdParty')

stage('yasm', """
mac:
    git clone https://github.com/yasm/yasm.git
    cd yasm
    git checkout 41762bea
    ./autogen.sh
    make $MAKE_THREADS_CNT
""", 'ThirdParty')

stage('lzma', """
win:
    git clone https://github.com/desktop-app/lzma.git
    cd lzma\\C\\Util\\LzmaLib
    msbuild LzmaLib.sln /property:Configuration=Debug /property:Platform="$X8664"
release:
    msbuild LzmaLib.sln /property:Configuration=Release /property:Platform="$X8664"
""")

stage('xz', """
!win:
    git clone -b v5.2.5 https://git.tukaani.org/xz.git
    cd xz
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -B build . \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \\
        -D CMAKE_INSTALL_PREFIX:STRING=$USED_PREFIX
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

stage('zlib', """
    git clone https://github.com/desktop-app/zlib.git
    cd zlib
win:
    cd contrib\\vstudio\\vc14
    msbuild zlibstat.vcxproj /property:Configuration=Debug /property:Platform="%X8664%"
release:
    msbuild zlibstat.vcxproj /property:Configuration=ReleaseWithoutAsm /property:Platform="%X8664%"
mac:
    CFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure \\
        --static \\
        --prefix=$USED_PREFIX \\
        --archs="-arch x86_64 -arch arm64"
    make $MAKE_THREADS_CNT
    make install
""")

stage('mozjpeg', """
    git clone -b v4.0.3 https://github.com/mozilla/mozjpeg.git
    cd mozjpeg
win:
    cmake . ^
        -A %WIN32X64% ^
        -DWITH_JPEG8=ON ^
        -DPNG_SUPPORTED=OFF
    cmake --build . --config Debug
release:
    cmake --build . --config Release
mac:
    CFLAGS="-arch arm64" cmake -B build.arm64 . \\
        -D CMAKE_SYSTEM_NAME=Darwin \\
        -D CMAKE_SYSTEM_PROCESSOR=arm64 \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D WITH_JPEG8=ON \\
        -D ENABLE_SHARED=OFF \\
        -D PNG_SUPPORTED=OFF
    cmake --build build.arm64 $MAKE_THREADS_CNT
    cmake -B build . \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D WITH_JPEG8=ON \\
        -D ENABLE_SHARED=OFF \\
        -D PNG_SUPPORTED=OFF
    cmake --build build $MAKE_THREADS_CNT
    lipo -create build.arm64/libjpeg.a build/libjpeg.a -output build/libjpeg.a
    lipo -create build.arm64/libturbojpeg.a build/libturbojpeg.a -output build/libturbojpeg.a
    cmake --install build
""")

stage('openssl', """
    git clone -b OpenSSL_1_1_1-stable https://github.com/openssl/openssl openssl
    cd openssl
win32:
    perl Configure no-shared no-tests debug-VC-WIN32
win64:
    perl Configure no-shared no-tests debug-VC-WIN64A
win:
    nmake
    mkdir out.dbg
    move libcrypto.lib out.dbg
    move libssl.lib out.dbg
    move ossl_static.pdb out.dbg
release:
    move out.dbg\\ossl_static.pdb out.dbg\\ossl_static
    nmake clean
    move out.dbg\\ossl_static out.dbg\\ossl_static.pdb
win32:
    perl Configure no-shared no-tests VC-WIN32
win64:
    perl Configure no-shared no-tests VC-WIN64A
win:
    nmake
    mkdir out
    move libcrypto.lib out
    move libssl.lib out
    move ossl_static.pdb out
mac:
    ./Configure --prefix=$USED_PREFIX no-shared no-tests darwin64-arm64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
    mkdir out.arm64
    mv libssl.a out.arm64
    mv libcrypto.a out.arm64
    make clean
    ./Configure --prefix=$USED_PREFIX no-shared no-tests darwin64-x86_64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
    mkdir out.x86_64
    mv libssl.a out.x86_64
    mv libcrypto.a out.x86_64
    lipo -create out.arm64/libcrypto.a out.x86_64/libcrypto.a -output libcrypto.a
    lipo -create out.arm64/libssl.a out.x86_64/libssl.a -output libssl.a
""")

stage('opus', """
    git clone -b v1.3.1 https://github.com/xiph/opus.git
    cd opus
    git cherry-pick 927de8453c
win:
    cmake -B out . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local/opus ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG"
    cmake --build out --config Debug
release:
    cmake --build out --config Release
    cmake --install out --config Release
mac:
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -B build . \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \\
        -D CMAKE_INSTALL_PREFIX:STRING=$USED_PREFIX
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

stage('rnnoise', """
    git clone https://github.com/desktop-app/rnnoise.git
    cd rnnoise
    mkdir out
    cd out
win:
    cmake -A %WIN32X64% ..
    cmake --build . --config Debug
release:
    cmake --build . --config Release
!win:
    mkdir Debug
    cd Debug
    cmake -G Ninja ../.. \\
        -D CMAKE_BUILD_TYPE=Debug \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja ../.. \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    ninja
""")

stage('libiconv', """
mac:
    VERSION=1.16
    rm -f libiconv.tar.gz
    wget -O libiconv.tar.gz https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$VERSION.tar.gz
    rm -rf libiconv-$VERSION
    tar -xvzf libiconv.tar.gz
    rm libiconv.tar.gz
    mv libiconv-$VERSION libiconv
    cd libiconv
    CFLAGS="$MIN_VER $UNGUARDED -arch arm64" CPPFLAGS="$MIN_VER $UNGUARDED -arch arm64" LDFLAGS="$MIN_VER" ./configure --enable-static --host=arm --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    mkdir out.arm64
    mv lib/.libs/libiconv.a out.arm64
    make clean
    CFLAGS="$MIN_VER $UNGUARDED" CPPFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --enable-static --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    lipo -create out.arm64/libiconv.a lib/.libs/libiconv.a -output lib/.libs/libiconv.a
    make install
""")

stage('ffmpeg', """
    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/4.4
win:
    SET PATH_BACKUP_=%PATH%
    SET PATH=%ROOT_DIR%\\ThirdParty\\msys64\\usr\\bin;%PATH%

    set CHERE_INVOKING=enabled_from_arguments
    set MSYS2_PATH_TYPE=inherit

depends:patches/build_ffmpeg_win.sh
    bash --login ../patches/build_ffmpeg_win.sh

    SET PATH=%PATH_BACKUP_%
mac:
depends:yasm/yasm
    ./configure --prefix=$USED_PREFIX \
    --enable-cross-compile \
    --target-os=darwin \
    --arch="arm64" \
    --extra-cflags="$MIN_VER -arch arm64 $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
    --extra-cxxflags="$MIN_VER -arch arm64 $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
    --extra-ldflags="$MIN_VER -arch arm64 $USED_PREFIX/lib/libopus.a" \
    --enable-protocol=file \
    --enable-libopus \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-everything \
    --enable-hwaccel=h264_videotoolbox \
    --enable-hwaccel=hevc_videotoolbox \
    --enable-hwaccel=mpeg1_videotoolbox \
    --enable-hwaccel=mpeg2_videotoolbox \
    --enable-hwaccel=mpeg4_videotoolbox \
    --enable-decoder=aac \
    --enable-decoder=aac_at \
    --enable-decoder=aac_fixed \
    --enable-decoder=aac_latm \
    --enable-decoder=aasc \
    --enable-decoder=alac \
    --enable-decoder=alac_at \
    --enable-decoder=flac \
    --enable-decoder=gif \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=mp1 \
    --enable-decoder=mp1float \
    --enable-decoder=mp2 \
    --enable-decoder=mp2float \
    --enable-decoder=mp3 \
    --enable-decoder=mp3adu \
    --enable-decoder=mp3adufloat \
    --enable-decoder=mp3float \
    --enable-decoder=mp3on4 \
    --enable-decoder=mp3on4float \
    --enable-decoder=mpeg4 \
    --enable-decoder=msmpeg4v2 \
    --enable-decoder=msmpeg4v3 \
    --enable-decoder=opus \
    --enable-decoder=pcm_alaw \
    --enable-decoder=pcm_alaw_at \
    --enable-decoder=pcm_f32be \
    --enable-decoder=pcm_f32le \
    --enable-decoder=pcm_f64be \
    --enable-decoder=pcm_f64le \
    --enable-decoder=pcm_lxf \
    --enable-decoder=pcm_mulaw \
    --enable-decoder=pcm_mulaw_at \
    --enable-decoder=pcm_s16be \
    --enable-decoder=pcm_s16be_planar \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_s16le_planar \
    --enable-decoder=pcm_s24be \
    --enable-decoder=pcm_s24daud \
    --enable-decoder=pcm_s24le \
    --enable-decoder=pcm_s24le_planar \
    --enable-decoder=pcm_s32be \
    --enable-decoder=pcm_s32le \
    --enable-decoder=pcm_s32le_planar \
    --enable-decoder=pcm_s64be \
    --enable-decoder=pcm_s64le \
    --enable-decoder=pcm_s8 \
    --enable-decoder=pcm_s8_planar \
    --enable-decoder=pcm_u16be \
    --enable-decoder=pcm_u16le \
    --enable-decoder=pcm_u24be \
    --enable-decoder=pcm_u24le \
    --enable-decoder=pcm_u32be \
    --enable-decoder=pcm_u32le \
    --enable-decoder=pcm_u8 \
    --enable-decoder=vorbis \
    --enable-decoder=wavpack \
    --enable-decoder=wmalossless \
    --enable-decoder=wmapro \
    --enable-decoder=wmav1 \
    --enable-decoder=wmav2 \
    --enable-decoder=wmavoice \
    --enable-encoder=libopus \
    --enable-parser=aac \
    --enable-parser=aac_latm \
    --enable-parser=flac \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-parser=mpeg4video \
    --enable-parser=mpegaudio \
    --enable-parser=opus \
    --enable-parser=vorbis \
    --enable-demuxer=aac \
    --enable-demuxer=flac \
    --enable-demuxer=gif \
    --enable-demuxer=h264 \
    --enable-demuxer=hevc \
    --enable-demuxer=m4v \
    --enable-demuxer=mov \
    --enable-demuxer=mp3 \
    --enable-demuxer=ogg \
    --enable-demuxer=wav \
    --enable-muxer=ogg \
    --enable-muxer=opus

    make $MAKE_THREADS_CNT

    mkdir out.arm64
    mv libavformat/libavformat.a out.arm64
    mv libavcodec/libavcodec.a out.arm64
    mv libswresample/libswresample.a out.arm64
    mv libswscale/libswscale.a out.arm64
    mv libavutil/libavutil.a out.arm64

    make clean

    ./configure --prefix=$USED_PREFIX \
    --extra-cflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
    --extra-cxxflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
    --extra-ldflags="$MIN_VER $USED_PREFIX/lib/libopus.a" \
    --enable-protocol=file \
    --enable-libopus \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-everything \
    --enable-hwaccel=h264_videotoolbox \
    --enable-hwaccel=hevc_videotoolbox \
    --enable-hwaccel=mpeg1_videotoolbox \
    --enable-hwaccel=mpeg2_videotoolbox \
    --enable-hwaccel=mpeg4_videotoolbox \
    --enable-decoder=aac \
    --enable-decoder=aac_at \
    --enable-decoder=aac_fixed \
    --enable-decoder=aac_latm \
    --enable-decoder=aasc \
    --enable-decoder=alac \
    --enable-decoder=alac_at \
    --enable-decoder=flac \
    --enable-decoder=gif \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=mp1 \
    --enable-decoder=mp1float \
    --enable-decoder=mp2 \
    --enable-decoder=mp2float \
    --enable-decoder=mp3 \
    --enable-decoder=mp3adu \
    --enable-decoder=mp3adufloat \
    --enable-decoder=mp3float \
    --enable-decoder=mp3on4 \
    --enable-decoder=mp3on4float \
    --enable-decoder=mpeg4 \
    --enable-decoder=msmpeg4v2 \
    --enable-decoder=msmpeg4v3 \
    --enable-decoder=opus \
    --enable-decoder=pcm_alaw \
    --enable-decoder=pcm_alaw_at \
    --enable-decoder=pcm_f32be \
    --enable-decoder=pcm_f32le \
    --enable-decoder=pcm_f64be \
    --enable-decoder=pcm_f64le \
    --enable-decoder=pcm_lxf \
    --enable-decoder=pcm_mulaw \
    --enable-decoder=pcm_mulaw_at \
    --enable-decoder=pcm_s16be \
    --enable-decoder=pcm_s16be_planar \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_s16le_planar \
    --enable-decoder=pcm_s24be \
    --enable-decoder=pcm_s24daud \
    --enable-decoder=pcm_s24le \
    --enable-decoder=pcm_s24le_planar \
    --enable-decoder=pcm_s32be \
    --enable-decoder=pcm_s32le \
    --enable-decoder=pcm_s32le_planar \
    --enable-decoder=pcm_s64be \
    --enable-decoder=pcm_s64le \
    --enable-decoder=pcm_s8 \
    --enable-decoder=pcm_s8_planar \
    --enable-decoder=pcm_u16be \
    --enable-decoder=pcm_u16le \
    --enable-decoder=pcm_u24be \
    --enable-decoder=pcm_u24le \
    --enable-decoder=pcm_u32be \
    --enable-decoder=pcm_u32le \
    --enable-decoder=pcm_u8 \
    --enable-decoder=vorbis \
    --enable-decoder=wavpack \
    --enable-decoder=wmalossless \
    --enable-decoder=wmapro \
    --enable-decoder=wmav1 \
    --enable-decoder=wmav2 \
    --enable-decoder=wmavoice \
    --enable-encoder=libopus \
    --enable-parser=aac \
    --enable-parser=aac_latm \
    --enable-parser=flac \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-parser=mpeg4video \
    --enable-parser=mpegaudio \
    --enable-parser=opus \
    --enable-parser=vorbis \
    --enable-demuxer=aac \
    --enable-demuxer=flac \
    --enable-demuxer=gif \
    --enable-demuxer=h264 \
    --enable-demuxer=hevc \
    --enable-demuxer=m4v \
    --enable-demuxer=mov \
    --enable-demuxer=mp3 \
    --enable-demuxer=ogg \
    --enable-demuxer=wav \
    --enable-muxer=ogg \
    --enable-muxer=opus

    make $MAKE_THREADS_CNT

    lipo -create out.arm64/libavformat.a libavformat/libavformat.a -output libavformat/libavformat.a
    lipo -create out.arm64/libavcodec.a libavcodec/libavcodec.a -output libavcodec/libavcodec.a
    lipo -create out.arm64/libswresample.a libswresample/libswresample.a -output libswresample/libswresample.a
    lipo -create out.arm64/libswscale.a libswscale/libswscale.a -output libswscale/libswscale.a
    lipo -create out.arm64/libavutil.a libavutil/libavutil.a -output libavutil/libavutil.a

    make install
""")

stage('openal-soft', """
version: 2
    git clone -b wasapi_exact_device_time https://github.com/telegramdesktop/openal-soft.git
    cd openal-soft
    cd build
win:
    cmake .. ^
        -A %WIN32X64% ^
        -D LIBTYPE:STRING=STATIC ^
        -D FORCE_STATIC_VCRT=ON
    msbuild OpenAL.vcxproj /property:Configuration=Debug /property:Platform="%WIN32X64%"
release:
    msbuild OpenAL.vcxproj /property:Configuration=RelWithDebInfo /property:Platform="%WIN32X64%"
mac:
    CFLAGS=$UNGUARDED CPPFLAGS=$UNGUARDED cmake .. \\
        -D CMAKE_INSTALL_PREFIX:PATH=$USED_PREFIX \\
        -D ALSOFT_EXAMPLES=OFF \\
        -D LIBTYPE:STRING=STATIC \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    make $MAKE_THREADS_CNT
    make install
""")

if 'build-stackwalk' in options:
    stage('stackwalk', """
mac:
    git clone https://chromium.googlesource.com/breakpad/breakpad stackwalk
    cd stackwalk
    git checkout dfcb7b6799
depends:patches/breakpad.diff
    git apply ../patches/breakpad.diff
    git clone -b release-1.11.0 https://github.com/google/googletest src/testing
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout e1e7b0ad8e
    cd ../../build
    python3 gyp_breakpad
    cd ../processor
    xcodebuild -project processor.xcodeproj -target minidump_stackwalk -configuration Release build
""")

stage('breakpad', """
    git clone https://chromium.googlesource.com/breakpad/breakpad
    cd breakpad
    git checkout dfcb7b6799
depends:patches/breakpad.diff
    git apply ../patches/breakpad.diff
    git clone -b release-1.11.0 https://github.com/google/googletest src/testing
win:
    if "%X8664%" equ "x64" (
        set "FolderPostfix=_x64"
    ) else (
        set "FolderPostfix="
    )
    cd src\\client\\windows
    gyp --no-circular-check breakpad_client.gyp --format=ninja
    cd ..\\..
    ninja -C out/Debug%FolderPostfix% common crash_generation_client exception_handler
release:
    ninja -C out/Release%FolderPostfix% common crash_generation_client exception_handler
    cd tools\\windows\\dump_syms
    gyp dump_syms.gyp --format=ninja
    cd ..\\..\\..
    ninja -C out/Release%FolderPostfix% dump_syms
mac:
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout e1e7b0ad8e
    cd ../../..
    cd src/client/mac
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Debug build
release:
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Release build
    cd ../../tools/mac/dump_syms
    xcodebuild -project dump_syms.xcodeproj -target dump_syms -configuration Release build
""")

stage('crashpad', """
mac:
    git clone https://github.com/desktop-app/crashpad.git
    cd crashpad
    git checkout c1b7afa2fd
    git submodule init
    git submodule update third_party/mini_chromium
    ZLIB_PATH=$USED_PREFIX/include
    ZLIB_LIB=$USED_PREFIX/lib/libz.a
    mkdir out
    cd out
    mkdir Debug.x86_64
    cd Debug.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Debug.arm64
    cd Debug.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Debug
    lipo -create Debug.arm64/crashpad_handler Debug.x86_64/crashpad_handler -output Debug/crashpad_handler
    lipo -create Debug.arm64/libcrashpad_client.a Debug.x86_64/libcrashpad_client.a -output Debug/libcrashpad_client.a
release:
    mkdir Release.x86_64
    cd Release.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Release.arm64
    cd Release.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Release
    lipo -create Release.arm64/crashpad_handler Release.x86_64/crashpad_handler -output Release/crashpad_handler
    lipo -create Release.arm64/libcrashpad_client.a Release.x86_64/libcrashpad_client.a -output Release/libcrashpad_client.a
""")

stage('tg_angle', """
win:
    git clone https://github.com/desktop-app/tg_angle.git
    cd tg_angle
    git checkout 0bb011f9e4
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja ^
        -DCMAKE_BUILD_TYPE=Debug ^
        -DTG_ANGLE_SPECIAL_TARGET=%SPECIAL_TARGET% ^
        -DTG_ANGLE_ZLIB_INCLUDE_PATH=%LIBS_DIR%/zlib ../..
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DTG_ANGLE_SPECIAL_TARGET=%SPECIAL_TARGET% ^
        -DTG_ANGLE_ZLIB_INCLUDE_PATH=%LIBS_DIR%/zlib ../..
    ninja
    cd ..\\..\\..
""")

if buildQt5:
    stage('qt_5_15_2', """
    git clone git://code.qt.io/qt/qt5.git qt_5_15_2
    cd qt_5_15_2
    perl init-repository --module-subset=qtbase,qtimageformats,qtsvg
    git checkout v5.15.2
    git submodule update qtbase qtimageformats qtsvg
depends:patches/qtbase_5_15_2/*.patch
    cd qtbase
win:
    for /r %%i in (..\\..\\patches\\qtbase_5_15_2\\*) do git apply %%i
    cd ..

    SET CONFIGURATIONS=-debug
release:
    SET CONFIGURATIONS=-debug-and-release
win:
    """ + removeDir("\"%LIBS_DIR%\\Qt-5.15.2\"") + """
    SET ANGLE_DIR=%LIBS_DIR%\\tg_angle
    SET ANGLE_LIBS_DIR=%ANGLE_DIR%\\out
    SET MOZJPEG_DIR=%LIBS_DIR%\\mozjpeg
    SET OPENSSL_DIR=%LIBS_DIR%\\openssl
    SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\\out
    SET ZLIB_LIBS_DIR=%LIBS_DIR%\\zlib\\contrib\\vstudio\\vc14\\%X8664%
    configure -prefix "%LIBS_DIR%\\Qt-5.15.2" ^
        %CONFIGURATIONS% ^
        -force-debug-info ^
        -opensource ^
        -confirm-license ^
        -static ^
        -static-runtime ^
        -opengl es2 -no-angle ^
        -I "%ANGLE_DIR%\\include" ^
        -D "KHRONOS_STATIC=" ^
        -D "DESKTOP_APP_QT_STATIC_ANGLE=" ^
        QMAKE_LIBS_OPENGL_ES2_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatDebug\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib" ^
        QMAKE_LIBS_OPENGL_ES2_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatReleaseWithoutAsm\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib" ^
        -egl ^
        QMAKE_LIBS_EGL_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatDebug\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        QMAKE_LIBS_EGL_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatReleaseWithoutAsm\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        -openssl-linked ^
        -I "%OPENSSL_DIR%\include" ^
        OPENSSL_LIBS_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libssl.lib %OPENSSL_LIBS_DIR%.dbg\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        OPENSSL_LIBS_RELEASE="%OPENSSL_LIBS_DIR%\libssl.lib %OPENSSL_LIBS_DIR%\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        -I "%MOZJPEG_DIR%" ^
        LIBJPEG_LIBS_DEBUG="%MOZJPEG_DIR%\Debug\jpeg-static.lib" ^
        LIBJPEG_LIBS_RELEASE="%MOZJPEG_DIR%\Release\jpeg-static.lib" ^
        -mp ^
        -nomake examples ^
        -nomake tests ^
        -platform win32-msvc

    jom -j16
    jom -j16 install
mac:
    find ../../patches/qtbase_5_15_2 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

    CONFIGURATIONS=-debug
release:
    CONFIGURATIONS=-debug-and-release
mac:
    ./configure -prefix "$USED_PREFIX/Qt-5.15.2" \
        $CONFIGURATIONS \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -static \
        -opengl desktop \
        -no-openssl \
        -securetransport \
        -I "$USED_PREFIX/include" \
        LIBJPEG_LIBS="$USED_PREFIX/lib/libjpeg.a" \
        ZLIB_LIBS="$USED_PREFIX/lib/libz.a" \
        -nomake examples \
        -nomake tests \
        -platform macx-clang

    make $MAKE_THREADS_CNT
    make install
""")

if buildQt6:
    stage('qt_6_2_0', """
mac:
    git clone -b v6.2.0 git://code.qt.io/qt/qt5.git qt_6_2_0
    cd qt_6_2_0
    perl init-repository --module-subset=qtbase,qtimageformats,qtsvg,qt5compat
depends:patches/qtbase_6_2_0/*.patch
    cd qtbase

    find ../../patches/qtbase_6_2_0 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

depends:patches/qt5compat_6_2_0/*.patch
    cd qt5compat

    find ../../patches/qt5compat_6_2_0 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

    CONFIGURATIONS=-debug
release:
    CONFIGURATIONS=-debug-and-release
mac:
    ./configure -prefix "$USED_PREFIX/Qt-6.2.0" \
        $CONFIGURATIONS \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -static \
        -opengl desktop \
        -no-openssl \
        -securetransport \
        -I "$USED_PREFIX/include" \
        -no-feature-futimens \
        -nomake examples \
        -nomake tests \
        -platform macx-clang -- -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"

    ninja
    ninja install
""")

stage('tg_owt', """
    git clone https://github.com/desktop-app/tg_owt.git
    cd tg_owt
    git checkout b02478677b
    git submodule init
    git submodule update src/third_party/libvpx/source/libvpx src/third_party/libyuv
win:
    SET MOZJPEG_PATH=$LIBS_DIR/mozjpeg
    SET OPUS_PATH=$LIBS_DIR/opus/include
    SET FFMPEG_PATH=$LIBS_DIR/ffmpeg
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
mac:
    MOZJPEG_PATH=$USED_PREFIX/include
    OPUS_PATH=$USED_PREFIX/include/opus
    FFMPEG_PATH=$USED_PREFIX/include
    mkdir out
    cd out
    mkdir Debug.x86_64
    cd Debug.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Debug.arm64
    cd Debug.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Debug
    lipo -create Debug.arm64/libtg_owt.a Debug.x86_64/libtg_owt.a -output Debug/libtg_owt.a
release:
    mkdir Release.x86_64
    cd Release.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Release.arm64
    cd Release.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Release
    lipo -create Release.arm64/libtg_owt.a Release.x86_64/libtg_owt.a -output Release/libtg_owt.a
""")

runStages()
