# CY-98a renderer/cache seam

Pixel Page offset; seven absolute-row cache slots; opaque clipped partial rows and title-less placeholders. Existing paging and row-step input retained for part 2. ASCII paint uses bounded stack text, without String construction; UTF-8 retains the existing strip loader and one reserved String scratch buffer. Codec pool untouched.

Checks pending on firmware commit; focused Classic UI and both compile-only targets. Geometry golden expected unchanged because row-step input/strip geometry is unchanged.

RAM/flash results pending. No device operations or push.
