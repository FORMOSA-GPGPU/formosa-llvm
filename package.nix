{ callPackage
}:

let
  formosa-llvm = callPackage ./formosa-llvm.nix { };
  compiler-rt = callPackage ./compiler-rt.nix {
    inherit formosa-llvm;
  };
  newlib = callPackage ./newlib.nix {
    inherit formosa-llvm;
  };
in
formosa-llvm.override {
  inherit compiler-rt newlib;
}
