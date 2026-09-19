# Flipper RPC schema (pinned)

Official protocol definitions from
[flipperdevices/flipperzero-protobuf](https://github.com/flipperdevices/flipperzero-protobuf)
revision `1c84fa48919cbb71d1cc65236fc0ee36740e24c6`.

That snapshot published no LICENSE file. The `.proto` files are stored
unmodified. Generated C++ is produced at build time and is not committed.

Regenerate nothing by hand. `CMakeLists.txt` runs `protoc --cpp_out` into the
build directory. Do not invent message bytes; encode `PB.Main` through the
generated bindings.

Milestone 2 uses ping, protobuf version, and storage info. Milestone 3 uses GUI
screen stream and input. Milestone 4 uses storage list, stat, read, write,
mkdir, rename, and delete. Application, GPIO, and desktop messages exist in the
schema because `flipper.proto` imports them. Milestone 6 calls
`app_start_request` and `app_exit_request`. GPIO and desktop messages are not
called yet.
