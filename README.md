# oXygen Mastering Suite

**oXygen** is a free and open-source professional mastering suite built with C++ and the JUCE framework. Designed for high fidelity and flexibility, it features AI-assisted automatic mastering functions, a modern Material Design 3 interface, a powerful 16-band graphic equalizer, and a suite of multiband dynamics processors.

![Version](https://img.shields.io/badge/version-0.0.1-alpha)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Platform](https://img.shields.io/badge/platform-macOS-blue)
![License](https://img.shields.io/badge/license-MIT-green)
[![ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Me-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/wamphyre94078)

![oXygen Logo](assets/oxygen_logo.svg)

## Features

### Core Functionality
- **AI-Assisted Mastering**: Automatically analyzes your track's loudness and tonal balance.
- **Smart Suggestions**: Provides target settings for gain and EQ to achieve a balanced, commercial-grade master.
- **16-Band Graphic Equalizer**: Precision control ranging from 30Hz to 25kHz with high-quality Peak filters.
- **Multiband Compressor**: 4 independent bands (Low, Low-Mid, High-Mid, High) with Linkwitz-Riley crossovers.
- **Stereo Imager**: Multiband width adjustment with mono compatibility safe for the low end.
- **Maximizer**: Transparent limiting to catch peaks without crushing dynamics.
- **Gain**: Simple, transparent input/output gain staging.

### Audio Engine
- **Modular Architecture**: Built on `juce::AudioProcessorGraph` for robust internal routing.
- **Linkwitz-Riley Crossovers**: Transparent frequency splitting (24dB/oct).
- **NaN/Silence Protection**: Optimized DSP processing with safety checks.
- **Auto-Detect**: Adjusts to Mono/Stereo configurations for DAW compatibility.

### Modern GUI (Material Design 3)
- **Vector Graphics**: Fully scalable SVG-based interface.
- **Theme Engine**: Centralized color and typography management.
- **Custom Fonts**: **Outfit** (Headings) and **Unbounded** (Body) for a clean, futuristic look.

## System Requirements

### macOS
- **OS**: macOS 10.13 or later
- **Format**: VST3, AU
- **Univeral**: Intel and Apple Silicon support

### Windows
- **OS**: Windows 10 or 11 (64-bit)
- **Format**: VST3

## 📦 Build from Source

### 🍎 macOS

#### Dependencies
*   **Xcode Command Line Tools** (install via `xcode-select --install`)
*   **CMake 3.20+** (install via `brew install cmake`)
*   **Git**

#### Build Steps
1.  **Clone the Repository**
    ```bash
    git clone https://github.com/Wamphyre/oXygen.git
    cd oXygen
    ```

2.  **Build**
    We provide a script to automate the configuration and build process:
    ```bash
    ./build.sh
    ```
    Alternatively, you can build manually:
    ```bash
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ```

3.  **Artifacts**
    The compiled plugin (VST3) will be located in:
    `releases/`

---

### 🪟 Windows

#### Dependencies
*   **Select "Desktop development with C++"** during installation.
*   **CMake 3.20+** (often included with VS, or install separately)
*   **Git**

#### Build Steps
1.  **Clone the Repository**
    Open PowerShell or Git Bash and run:
    ```powershell
    git clone https://github.com/Wamphyre/oXygen.git
    cd oXygen
    ```

2.  **Configure**
    This will automatically download the JUCE framework (if not present) and generate the Visual Studio solution.
    ```powershell
    cmake -B build
    ```

3.  **Build**
    Compile in Release mode for optimized performance.
    ```powershell
    cmake --build build --config Release
    ```

4.  **Artifacts**
    The plugin is automatically copied to the `releases/` folder:
    `releases/oXygen.vst3`

## Usage

### AI Mastering
1.  **Insert oXygen** on your master bus.
2.  **Play audio** through the plugin.
3.  **Analyze**: Let the AI listen to your track to suggest optimal settings.

### Manual Control
-   **EQ**: Adjust the 16 bands to shape the tonal balance.
-   **Dynamics**: Use the multiband compressor to control levels across frequency ranges.
-   **Width**: Use the Stereo Imager to widen the mix (avoid widening low frequencies).
-   **Limit**: Use the Maximizer to increase loudness while preventing clipping.

## Support & Donations
If you find oXygen useful and want to support its development, consider buying me a beer! ☕

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/wamphyre94078)

---

**Made with ❤️ for the audio community**
