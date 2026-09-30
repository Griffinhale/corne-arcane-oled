/*
 * A timeline, which is the shape WidgetKit asks for and the shape this world
 * already has.
 *
 * The world is a pure function of (seed, elapsed_ms). WidgetKit's timeline is
 * "give me the frame at time T". Those are the same shape, which is the whole
 * reason this port is worth doing on this platform rather than another.
 *
 * The one thing that has to be got right is that seeking independently from
 * zero for each of N entries costs N replays. A simulated hour is about 90 000
 * ticks and replays in roughly 22 ms, so a day is about half a second -- in
 * ONE pass. Advancing continuously and snapshotting at each boundary is that
 * one pass; N seeks from zero is the same work N times over, inside an
 * extension that will kill you for it.
 */

public struct CityFrame: Sendable {
    /// The world's clock at this entry, which is what a share link carries.
    public let worldMs: UInt32
    /// The animation phase, derived from the world's own cadence.
    public let frame: UInt32
    /// 8-bit grey, row-major, no padding, and only ever 0 or 255.
    public let pixels: [UInt8]
    public let width: Int
    public let height: Int

    public init(
        worldMs: UInt32, frame: UInt32, pixels: [UInt8], width: Int, height: Int
    ) {
        self.worldMs = worldMs
        self.frame = frame
        self.pixels = pixels
        self.width = width
        self.height = height
    }
}

/// A pixel-aligned window into a rendered frame.
///
/// Small ambient surfaces need a deliberate composition, not a scaled-down
/// town. Keeping the crop in source pixels preserves the renderer verbatim
/// while allowing each shell to choose the part of the world its shape can
/// actually carry.
public struct CityPixelRect: Equatable, Sendable {
    public let x: Int
    public let y: Int
    public let width: Int
    public let height: Int

    public init(x: Int, y: Int, width: Int, height: Int) {
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }
}

extension CityFrame {
    /// Return a source-pixel crop, or `nil` when the rectangle leaves the
    /// frame. This is presentation after rendering: it cannot alter renderer
    /// parity or the deterministic world named by `worldMs`.
    public func cropped(to rect: CityPixelRect) -> CityFrame? {
        guard rect.x >= 0, rect.y >= 0, rect.width > 0, rect.height > 0,
            rect.x + rect.width <= width, rect.y + rect.height <= height
        else { return nil }

        var cropped: [UInt8] = []
        cropped.reserveCapacity(rect.width * rect.height)
        for row in rect.y..<(rect.y + rect.height) {
            let start = row * width + rect.x
            cropped.append(contentsOf: pixels[start..<(start + rect.width)])
        }
        return CityFrame(
            worldMs: worldMs, frame: frame, pixels: cropped,
            width: rect.width, height: rect.height)
    }
}

extension City {
    /// Every entry in one forward pass. `boundaries` must ascend and must not
    /// reach back before the world's current clock: this replays history, it
    /// does not rewind it.
    public func timeline(at boundaries: [UInt32]) throws -> [CityFrame] {
        var out: [CityFrame] = []
        out.reserveCapacity(boundaries.count)
        var previous = worldMs
        for target in boundaries {
            precondition(target >= previous, "a timeline runs forward")
            previous = target
            /* seek() carries the run-up, so each entry costs only the ticks
             * since the one before it. This is the whole trick. */
            try seek(to: target)
            let frame = target / City.frameIntervalMs
            out.append(
                CityFrame(
                    worldMs: target, frame: frame, pixels: try render(frame: frame),
                    width: width, height: height))
        }
        return out
    }

    /// The common case: `count` entries, `everyMs` apart, starting from where
    /// the world already stands.
    public func timeline(everyMs interval: UInt32, count: Int) throws -> [CityFrame] {
        let step = City.frameIntervalMs
        /* Boundaries land on tick edges or the world would be asked for a
         * moment between two of its own frames. */
        let stride = (interval / step) * step
        precondition(stride > 0, "an entry every less than one tick is not a timeline")
        let start = worldMs
        return try timeline(at: (1...max(count, 1)).map { start + UInt32($0) * stride })
    }
}

#if canImport(CoreGraphics)
    import CoreGraphics
    import Foundation

    extension CityFrame {
        /// The frame as an image, at its own size and with no interpolation.
        ///
        /// Render at scale 1 and magnify in the view layer -- `.interpolation(.none)`
        /// in SwiftUI, which is the exact equivalent of the browser shell's
        /// `image-rendering: pixelated`. Asking the renderer for a large scale
        /// produces pixels the compositor would have produced for nothing, and
        /// `duel_city_fit_scale(TOWN, 390, 844)` returns 1 on a phone anyway.
        public var image: CGImage? {
            guard let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
            return CGImage(
                width: width, height: height,
                bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: width,
                space: CGColorSpaceCreateDeviceGray(),
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue),
                provider: provider, decode: nil,
                shouldInterpolate: false, intent: .defaultIntent)
        }

        /// White renderer pixels as opaque white and black renderer pixels as
        /// transparent. Accessory widgets need this alpha-bearing form because
        /// watch faces tint artwork by replacing its colour while preserving
        /// alpha; an opaque black rectangle would obscure the face instead.
        public var templateImage: CGImage? {
            var rgba = [UInt8](repeating: 0, count: pixels.count * 4)
            for (index, pixel) in pixels.enumerated() {
                let offset = index * 4
                rgba[offset] = pixel
                rgba[offset + 1] = pixel
                rgba[offset + 2] = pixel
                rgba[offset + 3] = pixel
            }
            guard let provider = CGDataProvider(data: Data(rgba) as CFData) else { return nil }
            let bitmap = CGBitmapInfo.byteOrder32Big.union(
                CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue))
            return CGImage(
                width: width, height: height,
                bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: bitmap,
                provider: provider, decode: nil,
                shouldInterpolate: false, intent: .defaultIntent)
        }
    }
#endif
