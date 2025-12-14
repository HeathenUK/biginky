#!/bin/bash
# Helper script to convert audio files to WAV format compatible with ESP32-P4
# Requires: ffmpeg

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default settings
SAMPLE_RATE=44100
CHANNELS=1
BIT_DEPTH=16
OUTPUT_DIR="./wav_output"

# Function to print usage
usage() {
    echo "Usage: $0 [options] <input_files>"
    echo ""
    echo "Convert audio files to WAV format compatible with ESP32-P4"
    echo ""
    echo "Options:"
    echo "  -h, --help          Show this help message"
    echo "  -r, --rate RATE     Set sample rate (default: 44100 Hz)"
    echo "  -c, --channels CH   Set channels: 1 (mono) or 2 (stereo) (default: 1)"
    echo "  -o, --output DIR    Set output directory (default: ./wav_output)"
    echo ""
    echo "Examples:"
    echo "  $0 music.mp3                    # Convert single file"
    echo "  $0 *.mp3                        # Convert all MP3 files"
    echo "  $0 -c 2 -r 48000 audio/*.mp3   # Stereo at 48kHz"
    echo ""
}

# Check if ffmpeg is installed
if ! command -v ffmpeg &> /dev/null; then
    echo -e "${RED}Error: ffmpeg is not installed${NC}"
    echo "Install it with: sudo apt install ffmpeg  (Ubuntu/Debian)"
    echo "                 brew install ffmpeg      (macOS)"
    exit 1
fi

# Parse arguments
INPUT_FILES=()
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -r|--rate)
            SAMPLE_RATE="$2"
            shift 2
            ;;
        -c|--channels)
            CHANNELS="$2"
            if [[ "$CHANNELS" != "1" && "$CHANNELS" != "2" ]]; then
                echo -e "${RED}Error: Channels must be 1 (mono) or 2 (stereo)${NC}"
                exit 1
            fi
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        *)
            INPUT_FILES+=("$1")
            shift
            ;;
    esac
done

# Check if any input files provided
if [ ${#INPUT_FILES[@]} -eq 0 ]; then
    echo -e "${RED}Error: No input files specified${NC}"
    usage
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Convert each file
TOTAL=0
SUCCESS=0
FAILED=0

echo "==================================="
echo "Audio Conversion for ESP32-P4"
echo "==================================="
echo "Sample Rate:  ${SAMPLE_RATE} Hz"
echo "Channels:     ${CHANNELS} ($([[ $CHANNELS -eq 1 ]] && echo "mono" || echo "stereo"))"
echo "Bit Depth:    ${BIT_DEPTH}-bit"
echo "Output Dir:   ${OUTPUT_DIR}"
echo "==================================="
echo ""

for input_file in "${INPUT_FILES[@]}"; do
    TOTAL=$((TOTAL + 1))
    
    # Check if file exists
    if [ ! -f "$input_file" ]; then
        echo -e "${RED}[SKIP]${NC} $input_file (not found)"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Get base filename without extension
    filename=$(basename -- "$input_file")
    basename="${filename%.*}"
    output_file="${OUTPUT_DIR}/${basename}.wav"
    
    echo -n "Converting: $filename ... "
    
    # Convert file
    if ffmpeg -i "$input_file" \
              -acodec pcm_s${BIT_DEPTH}le \
              -ar ${SAMPLE_RATE} \
              -ac ${CHANNELS} \
              -y \
              "$output_file" \
              -loglevel error 2>&1; then
        
        # Get file size
        size=$(du -h "$output_file" | cut -f1)
        
        # Get duration
        duration=$(ffprobe -i "$output_file" -show_entries format=duration -v quiet -of csv="p=0" 2>/dev/null | cut -d. -f1)
        
        echo -e "${GREEN}[OK]${NC} (${size}, ${duration}s)"
        SUCCESS=$((SUCCESS + 1))
    else
        echo -e "${RED}[FAIL]${NC}"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "==================================="
echo "Conversion Complete"
echo "==================================="
echo "Total:    $TOTAL files"
echo -e "Success:  ${GREEN}$SUCCESS${NC} files"
if [ $FAILED -gt 0 ]; then
    echo -e "Failed:   ${RED}$FAILED${NC} files"
fi
echo "==================================="
echo ""

if [ $SUCCESS -gt 0 ]; then
    echo -e "${GREEN}Converted files are in: $OUTPUT_DIR${NC}"
    echo "Copy these .wav files to your SD card root directory."
    echo ""
    echo "Don't forget to create media.txt to map images to audio files!"
fi
