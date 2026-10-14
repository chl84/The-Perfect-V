The 33 national flags in atlas.bmp were downloaded from FlagCDN (Flagpedia),
which supplies flag images based on Wikimedia Commons artwork:
https://flagpedia.net/download/api
https://commons.wikimedia.org/wiki/Category:Flags_of_countries

National flag designs are public-domain symbols. These are straight rectangular
icons, without shading or alteration to the symbols. Original aspect ratios are
preserved, including the square Swiss flag. England uses St George's cross;
GBR uses the Union Flag. Korea means South Korea; Holland means the Netherlands.
Belarus uses its current flag with the national ornament.

sources.json records each image URL, dimensions and SHA-256 of the source PNG.
Downloaded on 2026-10-14. To rebuild: python tools/generate_flag_atlas.py
(the development tool requires ffmpeg and network access).
The installed game uses only the bundled BMP; no network or SVG renderer is needed.
