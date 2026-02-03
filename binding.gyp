{
  "targets": [
    {
      "target_name": "inputhook",
      "sources": [
        "src/addon.cc",
        "src/common/emitter.cc"
      ],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include.slice(1, -1)\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS=1"
      ],
      "cflags_cc": ["-std=c++17"],
      "conditions": [
        ["OS=='win'", {
          "sources": [
            "src/platform/win/hook_win.cc"
          ],
          "libraries": [
            "-luser32"
          ],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }],
        ["OS=='mac'", {
          "sources": [
            "src/platform/mac/hook_mac.mm"
          ],
          "xcode_settings": {
            "OTHER_LDFLAGS": [
              "-framework", "ApplicationServices",
              "-framework", "CoreFoundation",
              "-framework", "CoreGraphics",
              "-framework", "Cocoa"
            ],
            "OTHER_CFLAGS": ["-std=c++17", "-fobjc-arc"],
            "OTHER_CPLUSPLUSFLAGS": ["-std=c++17"]
          }
        }],
        ["OS=='linux'", {
          "sources": [
            "src/platform/linux/hook_x11.cc"
          ],
          "libraries": [
            "-lX11",
            "-lXi"
          ]
        }]
      ]
    }
  ]
}
