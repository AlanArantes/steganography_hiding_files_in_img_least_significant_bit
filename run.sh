#!/bin/bash
#

help_text() {
  echo "Usage: $0 <command> <arg1> <arg2>"
  echo ""
  echo "Commands:"
  echo "  hide    Combine an image file with a generic file into one output."
  echo "           Arguments:"
  echo "             <arg1> - Path to the image file."
  echo "             <arg2> - Path to a generic file (executable, image, text, etc.)."
  echo ""
  echo "  extract  Extract a generic file from an image."
  echo "           Arguments:"
  echo "             <arg1> - Path to the image file."
  echo "             <arg2> - Name for the extracted file."
  echo ""
  echo "  mount    Mount an image file to a directory."
  echo "           Arguments:"
  echo "             <arg1> - Path to the image file."
  echo "             <arg2> - Path to the target directory."
  echo ""
  echo "Examples:"
  echo "  $0 hide image.png file.txt"
  echo "  $0 extract image.png output.txt"
  echo "  $0 mount image.png /mnt/mydir"
  echo ""
  echo "Note: Ensure proper permissions and valid paths for all arguments."
  exit 1
}

if [ $# -lt 3 ] || [[ "$1" != "hide" && "$1" != "extract" && "$1" != "mount" ]]; then
  help_text
  exit 1
fi

if [[ "$1" == "mount" ]]; then
  if [[ -f $2 && -d $3 ]]; then
    build/steganography $1 $2 $3 -f -d -s
    exit
  fi

  echo "Usage: $0 <command> <arg1> <arg2>"
  echo ""
  echo "  $0 mount image.png /mnt/mydir"
  echo "                     * needs to exist"
  echo ""
  echo "Note: Ensure proper permissions and valid paths for all arguments."
  exit 1
fi
