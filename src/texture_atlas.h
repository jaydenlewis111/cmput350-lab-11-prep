#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <cassert>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mSize{size} {};

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) {
        constexpr size_t failID = std::numeric_limits<size_t>::max();
        // Cannot fit even with the full width of the bin available.
        if (rectSize.x > mSize.x || rectSize.y > mSize.y) {
            return {false, failID};
        }
        if (mOffsetX + rectSize.x > mSize.x) {
            mLineY += mLineMaxHeight;
            mOffsetX = 0.f;
            mLineMaxHeight = 0.f;

        if (mLineY + rectSize.y > mSize.y) {
            return {false, failID};
        }
        mRects.emplace_back(sf::Vector2f{mOffsetX, mLineY}, rectSize);
        mOffsetX += rectSize.x;
        mLineMaxHeight = std::max(mLineMaxHeight, rectSize.y);
        return {true, mRects.size() - 1};
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    sf::FloatRect getBounds(size_t rectID) const {
        assert(rectID < mRects.size());
        return mRects[rectID];
    }

private:
    sf::Vector2f mSize;
    float mLineY = 0.f;
    float mOffsetX = 0.f;
    float mLineMaxHeight = 0.f;
    std::vector<sf::FloatRect> mRects;
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, uint pixels) {
    const sf::Vector2u orig = img.getSize();
    if (orig.x == 0 || orig.y == 0) {
        return img;
    }
    const sf::Vector2u newSize{orig.x + 2 * pixels, orig.y + 2 * pixels};
    sf::Image result(newSize, sf::Color::Transparent);
    for (unsigned y = 0; y < newSize.y; ++y) {
        // clamp (y - pixels) into [0, orig.y - 1] without unsigned underflow
        const unsigned srcY = y < pixels ? 0 : std::min(y - pixels, orig.y - 1);
        for (unsigned x = 0; x < newSize.x; ++x) {
            const unsigned srcX = x < pixels ? 0 : std::min(x - pixels, orig.x - 1);
            result.setPixel({x, y}, img.getPixel({srcX, srcY}));
        }
    }
    return result;
}

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, uint padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {
        // Texture memory is uninitialized on creation, clear it to transparent.
        mTexture.update(sf::Image(mTexture.getSize(), sf::Color::Transparent));
    }

    TextureID registerTexture(const std::string& filepath) {
        sf::Image img;
        if (!img.loadFromFile(filepath)) {
            throw std::runtime_error("TextureAtlas: failed to load image " + filepath);
        }
        const sf::Image padded = padImage(img, mPadding);
        const auto [ok, id] = mPacker.pack(sf::Vector2f(padded.getSize()));
        if (!ok) {
            throw std::runtime_error("TextureAtlas: no space left in atlas for " + filepath);
        }
        const sf::FloatRect bounds = mPacker.getBounds(id);
        mTexture.update(padded, sf::Vector2u(bounds.position));
        return id;
    }

    // Bounds of the unpadded texture in OpenGL UV space
    sf::FloatRect getBoundsUV(TextureID id) const {
        const sf::FloatRect b = mPacker.getBounds(id);
        const sf::Vector2f atlasSize(mTexture.getSize());
        const float pad = static_cast<float>(mPadding);
        // unpadded rect in top-left-origin pixel coordinates
        const sf::Vector2f topLeft = b.position + sf::Vector2f{pad, pad};
        const sf::Vector2f size = b.size - sf::Vector2f{2 * pad, 2 * pad};
        // flip y: bottom edge in pixel coords is topLeft.y + size.y
        const sf::Vector2f bottomLeftUV{topLeft.x / atlasSize.x,
                                        (atlasSize.y - (topLeft.y + size.y)) / atlasSize.y};
        return {bottomLeftUV, size / atlasSize};
    }

    sf::Vector2u getSizePixels(TextureID id) const {
        const sf::Vector2u padded(mPacker.getBounds(id).size);
        return {padded.x - 2 * mPadding, padded.y - 2 * mPadding};
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    uint mPadding;
};

#endif  // TEXTURE_ATLAS_H
