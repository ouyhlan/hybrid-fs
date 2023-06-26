#!/usr/bin/env bash

rm -rf ./build

# cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=YES  .
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=YES  .
if [[ "$?" != 0  ]];then
	exit
fi

cmake --build build 2>&1 | tee build.log


# https://stackoverflow.com/questions/22623045/return-value-of-redirected-bash-command
if [[ "${PIPESTATUS[0]}" != 0  ]];then
	cat ./build.log | grep --color "error"
	exit
fi

exit