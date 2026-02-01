#!/bin/bash

rm -f tags
ctags -R --c++-kinds=+p --fields=+S --extra=+q .
