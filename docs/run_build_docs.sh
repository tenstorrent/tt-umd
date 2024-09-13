if [[ -z "$TT_UMD_HOME" ]]; then
  echo "Must provide TT_UMD_HOME in environment" 1>&2
  exit 1
fi

echo "Building tt-umd docs..."

cd $TT_UMD_HOME
doxygen Doxyfile
