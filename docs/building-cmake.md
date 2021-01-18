## Build instructions for CMake using Docker

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Clone source code

    git clone --recursive https://github.com/kotatogram/kotatogram-desktop.git

### Prepare libraries

Go to the `kotatogram-desktop` directory and run

    docker build -t kotatogram-desktop:centos_env Telegram/build/docker/centos_env/

### Building the project

Make sure that you're still in the `kotatogram-desktop` directory and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm \
        -v $PWD:/usr/src/tdesktop \
        kotatogram-desktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH \
        -D DESKTOP_APP_USE_PACKAGED=OFF \
        -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON

Or, to create a debug build, run (also using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm \
        -v $PWD:/usr/src/tdesktop \
        -e DEBUG=1 \
        kotatogram-desktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH \
        -D DESKTOP_APP_USE_PACKAGED=OFF \
        -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON

If you want to build with crash reporter, use `-D DESKTOP_APP_DISABLE_CRASH_REPORTS=OFF` instead of `-D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON`.

The built files will be in the `out` directory.

[api_credentials]: api_credentials.md
