#!/usr/bin/env sh

cd "$(dirname "$0")" || exit
PROJECT_ROOT=$(pwd)
COMMAND_NAME=$(basename "$0")

ARG_CLEAN=0
ARG_DRY_RUN=0
ARG_PARALLEL=1

BOOST_SRC_DIR="$PROJECT_ROOT/3rdParty/boost-src"
BOOST_GIT_REPO="https://github.com/boostorg/boost.git"
BOOST_GIT_TAG="boost-1.80.0"
BOOST_B2="$BOOST_SRC_DIR/b2"
BOOST_DIR="$PROJECT_ROOT/3rdParty/boost"
BOOST_USER_CONFIG="$PROJECT_ROOT/user-config.jam"

USAGE=$(
  cat <<-EOS
  Name: $COMMAND_NAME - Clone and build Boost library

  Usage: $COMMAND_NAME [OPTION...]

  Options:
    -h|--help
    -d|--dry-run
    -j N            Run up to N commands in parallel
    --clean         Remove build files
EOS
)

run_safe() {
  if [ $ARG_DRY_RUN = 1 ]; then
    echo "** dry run: $*"
  else
    echo "** run: $*"
    eval "$*"
    status=$?
    if [ $status != 0 ]; then
      exit $status
    fi
  fi
}

help() {
  printf "%s\n" "$USAGE"
  exit "$1"
}

clean() {
  run_safe rm -rf "$BOOST_SRC_DIR"
  run_safe rm -rf "$BOOST_DIR"
}

clone_boost() {
  if [ ! -d "$BOOST_SRC_DIR" ]; then
    run_safe git clone --depth=1 -b $BOOST_GIT_TAG $BOOST_GIT_REPO "$BOOST_SRC_DIR"
  fi
  run_safe cd "$BOOST_SRC_DIR"
  run_safe git submodule init
  run_safe git submodule update --recursive --recommend-shallow -f
  run_safe cd "$PROJECT_ROOT"

}

bootstrap_boost() {
  if [ ! -f "$BOOST_B2" ]; then
    run_safe cd "$BOOST_SRC_DIR"
    run_safe ./bootstrap.sh
    run_safe cd "$PROJECT_ROOT"
  fi
}

boost_build() {
  run_safe cd "$BOOST_SRC_DIR"
  run_safe ./b2 install toolset=clang target-os=android link=static -j $ARG_PARALLEL --user-config="$BOOST_USER_CONFIG" --prefix="$BOOST_DIR"
  run_safe cd "$PROJECT_ROOT"
}

while [ $# -gt 0 ]; do
  case "$1" in
  -h | --help)
    help 0
    ;;
  -d | --dry-run)
    ARG_DRY_RUN=1
    ;;
  -j)
    ARG_PARALLEL="$2"
    shift
    ;;
  --clean)
    ARG_CLEAN=1
    ;;
  *)
    help 1
    ;;
  esac
  shift
done

if [ $ARG_CLEAN = 1 ]; then
  clean
  exit 0
fi

# Clone Boost
clone_boost

# bootstrap Boost
bootstrap_boost

# Build Boost
boost_build
