#pragma once

namespace oxygen
{
    namespace Assets
    {
        static const char* oxygenLogoSvg = R"svg(<svg width="800" height="300" viewBox="0 0 800 300" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="neonGrad" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" style="stop-color:#00FFFF;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#FF00FF;stop-opacity:1" />
    </linearGradient>
    <filter id="glow">
      <feGaussianBlur stdDeviation="2.5" result="coloredBlur"/>
      <feMerge>
        <feMergeNode in="coloredBlur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>
  <rect width="800" height="300" fill="#121212" rx="15" ry="15"/>
  <g transform="translate(100, 150) scale(1.5)" filter="url(#glow)">
    <path d="M-30,-30 L30,30 M-30,30 L30,-30" stroke="url(#neonGrad)" stroke-width="8" stroke-linecap="round" />
    <path d="M-40,0 L-20,0 M20,0 L40,0 M0,-40 L0,-20 M0,20 L0,40" stroke="cyan" stroke-width="2" opacity="0.5"/>
  </g>
  <text x="220" y="180" font-family="Arial, sans-serif" font-size="120" font-weight="bold" fill="white" letter-spacing="5">
    o<tspan fill="url(#neonGrad)">X</tspan>ygen
  </text>
  <text x="225" y="230" font-family="Arial, sans-serif" font-size="24" fill="#888888" letter-spacing="8">MASTERING SUITE</text>
</svg>)svg";

        static const char* oxygenIconSvg = R"svg(<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="neonGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#00FFFF;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#FF00FF;stop-opacity:1" />
    </linearGradient>
    <filter id="glow">
      <feGaussianBlur stdDeviation="15" result="coloredBlur"/>
      <feMerge>
        <feMergeNode in="coloredBlur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>
  <rect width="512" height="512" rx="100" ry="100" fill="#0a0a0a" stroke="#333" stroke-width="2"/>
  <circle cx="256" cy="256" r="180" stroke="url(#neonGrad)" stroke-width="10" fill="none" opacity="0.8" filter="url(#glow)"/>
  <circle cx="256" cy="256" r="160" stroke="#121212" stroke-width="20" fill="none"/>
  <g transform="translate(256, 256) scale(2.5)" filter="url(#glow)">
     <path d="M-40,-40 L40,40 M-40,40 L40,-40" stroke="white" stroke-width="12" stroke-linecap="round" />
     <path d="M-40,-40 L40,40 M-40,40 L40,-40" stroke="url(#neonGrad)" stroke-width="6" stroke-linecap="round" />
  </g>
</svg>)svg";
    }
}
