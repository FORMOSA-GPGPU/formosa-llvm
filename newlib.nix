{ stdenv
, formosa-llvm
, texinfo
}:

stdenv.mkDerivation {
  name = "newlib";
  src = builtins.fetchGit {
    url = "https://github.com/bminor/newlib.git";
    rev = "363357c023ce01e936bdaedf0f479292a8fa4e0f";
    ref = "master";
    shallow = true;
  };

  enableParallelBuilding = true;

  configurePhase = ''
    ./configure --prefix=$out \
    --target=riscv64-unknown-elf \
    CC_FOR_TARGET="${formosa-llvm}/bin/clang -march="rv64im_zicsr_zicond" \
    -mno-relax -mcmodel=medany \
    -Wno-error-implicit-function-declaration \
    -Wno-unused-command-line-argument \
    -Wno-error=int-conversion" \
    AS_FOR_TARGET="${formosa-llvm}/bin/llvm-as -march=rv64im -mabi=lp64" \
    AR_FOR_TARGET=${formosa-llvm}/bin/llvm-ar \
    LD_FOR_TARGET=${formosa-llvm}/bin/llvm-ld \
    RANLIB_FOR_TARGET=${formosa-llvm}/bin/llvm-ranlib
  '';

  buildInputs = [
    formosa-llvm
    texinfo
  ];
}
