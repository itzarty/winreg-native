{
  "targets": [
    {
      "target_name": "winreg",
      "sources": [ "winreg.cpp" ],
      "include_dirs": [
        "<!(node -p \"require('path').dirname(require.resolve('nan'))\")"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": { "ExceptionHandling": 1 }
          }
        }]
      ]
    }
  ]
}
