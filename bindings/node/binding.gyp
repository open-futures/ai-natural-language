{
  "targets": [
    {
      "target_name": "tts",
      "sources": ["index.cpp"],
      "include_dirs": ["<!(node -p \"require('path').join(__dirname, '..', '..', 'include')\")"],
      "libraries": ["-L<!(node -p \"require('path').join(__dirname, '..', '..', 'lib')\")", "-ltts"],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": ["-std=c++23", "-fexceptions"]
    }
  ]
}
