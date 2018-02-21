#include "graphics/texture.h"
#include "graphics/graphics.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

GLenum lovrTextureFormatGetGLFormat(TextureFormat format) {
  switch (format) {
    case FORMAT_RGB: return GL_RGB;
    case FORMAT_RGBA: return GL_RGBA;
    case FORMAT_RGBA16F: return GL_RGBA;
    case FORMAT_RGBA32F: return GL_RGBA;
    case FORMAT_RG11B10F: return GL_RGB;
    case FORMAT_DXT1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case FORMAT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    case FORMAT_DXT5: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
  }
}

GLenum lovrTextureFormatGetGLInternalFormat(TextureFormat format, bool srgb) {
  switch (format) {
    case FORMAT_RGB: return srgb ? GL_SRGB8 : GL_RGB8;
    case FORMAT_RGBA: return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    case FORMAT_RGBA16F: return GL_RGBA16F;
    case FORMAT_RGBA32F: return GL_RGBA32F;
    case FORMAT_RG11B10F: return GL_R11F_G11F_B10F;
    case FORMAT_DXT1: return srgb ? GL_COMPRESSED_SRGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case FORMAT_DXT3: return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    case FORMAT_DXT5: return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
  }
}

bool lovrTextureFormatIsCompressed(TextureFormat format) {
  return format == FORMAT_DXT1 || format == FORMAT_DXT3 || format == FORMAT_DXT5;
}

static void lovrTextureAllocate(Texture* texture, TextureData* textureData) {
  texture->allocated = true;
  texture->width = textureData->width;
  texture->height = textureData->height;

  if (lovrTextureFormatIsCompressed(textureData->format)) {
    return;
  }

  int w = textureData->width;
  int h = textureData->height;
  int mipmapCount = log2(MAX(w, h)) + 1;
  bool srgb = lovrGraphicsIsGammaCorrect() && texture->srgb;
  GLenum glFormat = lovrTextureFormatGetGLFormat(textureData->format);
  GLenum internalFormat = lovrTextureFormatGetGLInternalFormat(textureData->format, srgb);
#ifndef EMSCRIPTEN
  if (GLAD_GL_ARB_texture_storage) {
#endif
    glTexStorage2D(texture->type, mipmapCount, internalFormat, w, h);
#ifndef EMSCRIPTEN
  } else {
    for (int i = 0; i < mipmapCount; i++) {
      if (texture->type == TEXTURE_CUBE) {
        for (int face = 0; face < 6; face++) {
          glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, i, internalFormat, w, h, 0, glFormat, GL_UNSIGNED_BYTE, NULL);
        }
      } else {
        glTexImage2D(texture->type, i, internalFormat, w, h, 0, glFormat, GL_UNSIGNED_BYTE, NULL);
      }
      w = MAX(w >> 1, 1);
      h = MAX(h >> 1, 1);
    }
  }
#endif
}

Texture* lovrTextureCreate(TextureType type, TextureData** slices, int sliceCount, bool srgb, bool mipmaps) {
  Texture* texture = lovrAlloc(sizeof(Texture), lovrTextureDestroy);
  if (!texture) return NULL;

  texture->type = type;
  texture->slices = calloc(sliceCount, sizeof(TextureData**));
  texture->sliceCount = sliceCount;
  texture->srgb = srgb;
  texture->mipmaps = mipmaps;
  texture->allocated = false;

  WrapMode wrap = type == TEXTURE_CUBE ? WRAP_CLAMP : WRAP_REPEAT;
  glGenTextures(1, &texture->id);
  lovrGraphicsBindTexture(texture, texture->type, 0);
  lovrTextureSetFilter(texture, lovrGraphicsGetDefaultFilter());
  lovrTextureSetWrap(texture, (TextureWrap) { .s = wrap, .t = wrap, .r = wrap });

  if (slices) {
    for (int i = 0; i < sliceCount; i++) {
      lovrTextureReplacePixels(texture, slices[i], i);
    }
  }

  return texture;
}

void lovrTextureDestroy(const Ref* ref) {
  Texture* texture = containerof(ref, Texture);
  for (int i = 0; i < texture->sliceCount; i++) {
    if (&texture->slices[i]) {
      lovrRelease(&texture->slices[i]->ref);
    }
  }
  glDeleteTextures(1, &texture->id);
  free(texture->slices);
  free(texture);
}

void lovrTextureReplacePixels(Texture* texture, TextureData* textureData, int slice) {
  lovrRetain(&textureData->ref);
  if (texture->slices[slice]) {
    lovrRelease(&texture->slices[slice]->ref);
  }

  texture->slices[slice] = textureData;

  if (!texture->allocated) {
    lovrTextureAllocate(texture, textureData);
  } else {
    // validation
  }

  GLenum glFormat = lovrTextureFormatGetGLFormat(textureData->format);
  GLenum glInternalFormat = lovrTextureFormatGetGLInternalFormat(textureData->format, texture->srgb);
  GLenum binding = (texture->type == TEXTURE_CUBE) ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + slice : texture->type;

  if (lovrTextureFormatIsCompressed(textureData->format)) {
    Mipmap m; int i;
    vec_foreach(&textureData->mipmaps, m, i) {
      glCompressedTexImage2D(binding, i, glInternalFormat, m.width, m.height, 0, m.size, m.data);
    }
  } else {
    glTexSubImage2D(binding, 0, 0, 0, textureData->width, textureData->height, glFormat, GL_UNSIGNED_BYTE, textureData->data);
    if (texture->mipmaps) {
      glGenerateMipmap(texture->type);
    }
  }
}

TextureFilter lovrTextureGetFilter(Texture* texture) {
  return texture->filter;
}

void lovrTextureSetFilter(Texture* texture, TextureFilter filter) {
  float anisotropy = filter.mode == FILTER_ANISOTROPIC ? MAX(filter.anisotropy, 1.) : 1.;
  lovrGraphicsBindTexture(texture, texture->type, 0);
  texture->filter = filter;

  switch (filter.mode) {
    case FILTER_NEAREST:
      glTexParameteri(texture->type, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(texture->type, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      break;

    case FILTER_BILINEAR:
      if (texture->mipmaps) {
        glTexParameteri(texture->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(texture->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      } else {
        glTexParameteri(texture->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(texture->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      }
      break;

    case FILTER_TRILINEAR:
    case FILTER_ANISOTROPIC:
      if (texture->mipmaps) {
        glTexParameteri(texture->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(texture->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      } else {
        glTexParameteri(texture->type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(texture->type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      }
      break;
  }

  glTexParameteri(texture->type, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
}

TextureWrap lovrTextureGetWrap(Texture* texture) {
  return texture->wrap;
}

void lovrTextureSetWrap(Texture* texture, TextureWrap wrap) {
  texture->wrap = wrap;
  lovrGraphicsBindTexture(texture, texture->type, 0);
  glTexParameteri(texture->type, GL_TEXTURE_WRAP_S, wrap.s);
  glTexParameteri(texture->type, GL_TEXTURE_WRAP_T, wrap.t);
  if (texture->type == TEXTURE_CUBE) {
    glTexParameteri(texture->type, GL_TEXTURE_WRAP_R, wrap.r);
  }
}
