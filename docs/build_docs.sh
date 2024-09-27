if [[ -z "$TT_UMD_HOME" ]]; then
  echo "Must provide TT_UMD_HOME in environment" 1>&2
  exit 1
fi

echo "Building tt-umd docs..."

pushd $TT_UMD_HOME
DOCS_BUILD_DIR="build"
if [ ! -d "$DOCS_BUILD_DIR" ]; then
    mkdir $DOCS_BUILD_DIR
fi
doxygen Doxyfile
popd