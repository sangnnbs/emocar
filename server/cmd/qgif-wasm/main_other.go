//go:build !(js && wasm)

package main

import (
	"fmt"
	"os"
)

func main() {
	fmt.Fprintln(os.Stderr, "build for the browser: GOOS=js GOARCH=wasm go build (see the Makefile's wasm target)")
	os.Exit(2)
}
