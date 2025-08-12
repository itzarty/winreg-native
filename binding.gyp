{
  "targets": [
    {
      "target_name": "winreg-native",
      "sources": [
        "src/convert.cpp",
        "src/registry.cpp",
        "src/main.cpp"
      ],
      "include_dirs": [
        "<!(node -p \"require('path').dirname(require.resolve('nan'))\")",
        "src"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "Optimization": 3,
          "InlineFunctionExpansion": 2,
          "FavorSizeOrSpeed": 1,
          "OmitFramePointers": "true",
          "EnableFiberSafeOptimizations": "true",
          "WholeProgramOptimization": "true",
          "AdditionalOptions": [
            "/GL",
            "/arch:AVX2"
          ]
        }
      }
    }
  ]
}
