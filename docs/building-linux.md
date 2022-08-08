## Build instructions for Linux using Docker

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Clone source code

    git clone --recursive https://github.com/kotatogram/kotatogram-desktop.git

### Prepare libraries

Install [poetry](https://python-poetry.org), go to the `kotatogram-desktop/Telegram/build/docker/centos_env` directory and run

    poetry install
    poetry run gen_dockerfile | DOCKER_BUILDKIT=1 docker build -t kotatogram-desktop:centos_env -

### Building the project

Go up to the `kotatogram-desktop` directory and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm -it \
        -v $PWD:/usr/src/tdesktop \
        kotatogram-desktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH \
        -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON

Or, to create a debug build, run (also using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm -it \
        -v $PWD:/usr/src/tdesktop \
        -e DEBUG=1 \
        kotatogram-desktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH \
        -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON

If you want to build with crash reporter, use `-D DESKTOP_APP_DISABLE_CRASH_REPORTS=OFF` instead of `-D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON`.

If you need a backward compatible binary (running on older OS like the official one), you should build the binary with LTO.  
To do this, add `-D CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` option.

The built files will be in the `out` directory.

[api_credentials]: api_credentials.md
