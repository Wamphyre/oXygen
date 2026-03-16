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
    <filter id="typeGlow" x="-30%" y="-50%" width="180%" height="220%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="8" result="blurCyan"/>
      <feFlood flood-color="#00FFFF" flood-opacity="0.55" result="cyanFlood"/>
      <feComposite in="cyanFlood" in2="blurCyan" operator="in" result="cyanGlow"/>
      <feGaussianBlur in="SourceAlpha" stdDeviation="14" result="blurMagenta"/>
      <feFlood flood-color="#FF00FF" flood-opacity="0.35" result="magentaFlood"/>
      <feComposite in="magentaFlood" in2="blurMagenta" operator="in" result="magentaGlow"/>
      <feGaussianBlur in="SourceAlpha" stdDeviation="2.5" result="blurWhite"/>
      <feFlood flood-color="#FFFFFF" flood-opacity="0.40" result="whiteFlood"/>
      <feComposite in="whiteFlood" in2="blurWhite" operator="in" result="whiteGlow"/>
      <feMerge>
        <feMergeNode in="magentaGlow"/>
        <feMergeNode in="cyanGlow"/>
        <feMergeNode in="whiteGlow"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>
  <g transform="translate(100, 150) scale(1.5)" filter="url(#glow)">
    <path d="M-30,-30 L30,30 M-30,30 L30,-30" stroke="url(#neonGrad)" stroke-width="8" stroke-linecap="round" />
    <path d="M-40,0 L-20,0 M20,0 L40,0 M0,-40 L0,-20 M0,20 L0,40" stroke="cyan" stroke-width="2" opacity="0.5"/>
  </g>
  <text x="220" y="180" font-family="Arial, sans-serif" font-size="120" font-weight="bold" fill="white" letter-spacing="5" filter="url(#typeGlow)">
    o<tspan fill="url(#neonGrad)">X</tspan>ygen
  </text>
  <text x="225" y="230" font-family="Arial, sans-serif" font-size="24" fill="#CFCFCF" letter-spacing="8" filter="url(#typeGlow)">MASTERING SUITE</text>
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

        static const char* oxygenReferenceIconSvg = R"svg(<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="matchGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#FF00FF;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#00FFFF;stop-opacity:1" />
    </linearGradient>
    <filter id="glow" x="-30%" y="-30%" width="160%" height="160%">
      <feGaussianBlur stdDeviation="10" result="coloredBlur"/>
      <feMerge>
        <feMergeNode in="coloredBlur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>
  <circle cx="256" cy="256" r="156" stroke="url(#matchGrad)" stroke-width="16" fill="none" opacity="0.82" filter="url(#glow)"/>
  <circle cx="256" cy="256" r="122" stroke="#181b24" stroke-width="8" fill="none"/>
  <path d="M126 205 C160 180 192 180 222 205 S284 230 316 205 S366 180 388 197" stroke="#FF4DDB" stroke-width="18" stroke-linecap="round" stroke-linejoin="round" fill="none" filter="url(#glow)"/>
  <path d="M126 307 C160 332 192 332 222 307 S284 282 316 307 S366 332 388 315" stroke="#28F0FF" stroke-width="18" stroke-linecap="round" stroke-linejoin="round" fill="none" filter="url(#glow)"/>
  <path d="M228 236 L286 294" stroke="#F6FBFF" stroke-width="14" stroke-linecap="round" filter="url(#glow)"/>
  <path d="M286 294 L286 258 M286 294 L250 294" stroke="#F6FBFF" stroke-width="14" stroke-linecap="round" stroke-linejoin="round" filter="url(#glow)"/>
</svg>)svg";
    }
}
