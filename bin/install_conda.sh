#!/bin/bash

install() {
    mkdir -p "$HOME/miniconda3"
    wget "https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-$1.sh" -O "$HOME/miniconda3/miniconda.sh"
    bash "$HOME/miniconda3/miniconda.sh" -b -u -p "$HOME/miniconda3"
    rm "$HOME/miniconda3/miniconda.sh"
    conda init --all
}

case $1 in
host)
    install "x86_64"
    ;;
dpu)
    install "aarch64"
    ;;
*)
    echo "Usage: $0 [host|dpu]"
    exit 1
    ;;
esac
