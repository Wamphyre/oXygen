# oXygen Mastering Suite

![oXygen Logo](oxygen_logo.svg)

**oXygen** is a professional, modular mastering plugin suite built with C++ and the JUCE framework. Designed for high fidelity and flexibility, it features a modern Material Design 3 interface, a powerful 16-band graphic equalizer, and a suite of multiband dynamics processors.

---

## 🚀 Features

### 🎛️ Audio Processing Modules
The plugin uses a flexible `AudioProcessorGraph` architecture, allowing for robust internal routing and mono/stereo compatibility.

1.  **16-Band Graphic Equalizer**
    *   **Precision Control**: 16 bands ranging from 30Hz to 25kHz.
    *   **Pro-Grade Filters**: High-quality Peak filters with +/- 12dB gain range.
    *   **Modern Interface**: Clean vertical sliders for intuitive frequency shaping.

2.  **Multiband Compressor**
    *   **4 Independent Bands**: Low, Low-Mid, High-Mid, High.
    *   **Linkwitz-Riley Crossovers**: Transparent frequency splitting (24dB/oct).
    *   **Full Parametric Control**: Threshold, Ratio, Attack, Release, and Gain per band.
    *   **Safety**: Automatic gain handling and silence protection.

3.  **Stereo Imager**
    *   **Multiband Width**: Adjust stereo width independently for 4 frequency bands.
    *   **Mono Compatibility**: Safe widening without phase cancellation artifacts in the low end.

4.  **Maximizer**
    *   **Transparent Limiting**: Catch peaks without crushing dynamics.
    *   **Controls**: Threshold, Ceiling, and Release for final loudness optimization.

5.  **Gain**: Simple, transparent input/output gain staging.

### 🎨 Modern GUI (Material Design 3)
*   **Vector Graphics**: Fully scalable SVG-based interface (Logo, Icons).
*   **Theme Engine**: Centralized `Theme.h` managing colors (Surface, Primary, OnSurface) and typography.
*   **Custom Fonts**: Uses **Outfit** (Headings) and **Unbounded** (Body) for a futuristic, clean look.
*   **Consistent Styling**: Uniform controls across all modules via custom `ModuleWrapper` and Editors.

### 🛠️ Technical Highlights
*   **Modular Architecture**: Built on `juce::AudioProcessorGraph` for complex signal routing.
*   **Robust Audio Engine**: Auto-detects Mono/Stereo configurations to ensure compatibility with all DAWs (Reaper, Ableton, FL Studio, etc.).
*   **Performance**: Optimized DSP processing with safety checks for NaN/Silence.
*   **Assets**: embedded binary assets for single-file portability.

---

## 📦 Building from Source

### Prerequisites
*   **Windows 10/11** (Tested environment)
*   **Visual Studio 2022** (with C++ Desktop Development workload)
*   **CMake 3.20+**
*   **Git**

### Step-by-Step Build Instructions

1.  **Clone the Repository**
    ```bash
    git clone https://github.com/yourusername/oXygen.git
    cd oXygen
    ```

2.  **Configure with CMake**
    This will automatically download JUCE (if not present) and generate the Visual Studio solution.
    ```bash
    cmake -B build
    ```

3.  **Build the Plugin**
    Compile in Release mode for optimized performance.
    ```bash
    cmake --build build --config Release
    ```

4.  **Locate Artifacts**
    The compiled VST3 plugin will be available at:
    `build/oXygen_artefacts/Release/VST3/oXygen.vst3`

---

## 🧪 Testing & Verification
We use a custom test suite to verify audio integrity:
*   **Signal Chain Verification**: Ensures audio passes through all modules without dropping to silence.
*   **Mono/Stereo Check**: Validates behavior when tracks are mono or stereo.
*   **Visual Verification**: GUI renders correctly at various scaling factors.

## 📜 License
Proprietary Mastering Suite. Created for advanced audio engineering workflows.

---
*Built with ❤️ using JUCE and C++20.*
