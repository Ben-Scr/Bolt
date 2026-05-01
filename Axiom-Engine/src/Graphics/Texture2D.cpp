#include "pch.hpp"
#include "Texture2D.hpp"
#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Axiom {

	static void ChooseInternalAndFormat(int channels, bool srgb, GLint& internalFmt, GLenum& dataFmt) {
		switch (channels) {
		case 1: internalFmt = GL_R8;  dataFmt = GL_RED;  break;
		case 2: internalFmt = GL_RG8; dataFmt = GL_RG;   break;
		case 3:
			internalFmt = srgb ? GL_SRGB8 : GL_RGB8;
			dataFmt = GL_RGB;
			break;
		case 4:
		default:
			internalFmt = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
			dataFmt = GL_RGBA;
			break;
		}
	}

	Texture2D::Texture2D(const char* path,
		Filter filter, Wrap wrapU, Wrap wrapV,
		bool generateMipmaps, bool srgb, bool flipVertical)
		: m_Filter(filter), m_WrapU(wrapU), m_WrapV(wrapV), m_HasMips(generateMipmaps)
	{
		Load(path, generateMipmaps, srgb, flipVertical);
		if (IsValid()) ApplySamplerParams();
	}

	Texture2D::~Texture2D() { Destroy(); }

	Texture2D::Texture2D(Texture2D&& o) noexcept {
		m_Tex = o.m_Tex; o.m_Tex = 0;
		m_Width = o.m_Width; m_Height = o.m_Height; m_Channels = o.m_Channels;
		m_Filter = o.m_Filter; m_WrapU = o.m_WrapU; m_WrapV = o.m_WrapV; m_HasMips = o.m_HasMips;
	}

	Texture2D& Texture2D::operator=(Texture2D&& o) noexcept {
		if (this != &o) {
			Destroy();
			m_Tex = o.m_Tex; o.m_Tex = 0;
			m_Width = o.m_Width; m_Height = o.m_Height; m_Channels = o.m_Channels;
			m_Filter = o.m_Filter; m_WrapU = o.m_WrapU; m_WrapV = o.m_WrapV; m_HasMips = o.m_HasMips;
		}
		return *this;
	}

	void Texture2D::Destroy() {
		if (m_Tex) {
			glDeleteTextures(1, &m_Tex);
			m_Tex = 0;
		}
		m_Width = m_Height = m_Channels = 0;
	}

	bool Texture2D::Load(const char* path, bool generateMipmaps, bool srgb, bool flipVertical) {
		Destroy();

		stbi_set_flip_vertically_on_load(flipVertical);

		int w = 0, h = 0, n = 0;
		unsigned char* pixels = stbi_load(path, &w, &h, &n, 0);
		stbi_set_flip_vertically_on_load(false);
		if (!pixels) {
			AIM_CORE_WARN_TAG("Texture2D", "Failed to load texture: {}", path);
			return false;
		}

		GLint internalFmt = GL_RGBA8;
		GLenum dataFmt = GL_RGBA;
		ChooseInternalAndFormat(n, srgb, internalFmt, dataFmt);


		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		glGenTextures(1, &m_Tex);
		glBindTexture(GL_TEXTURE_2D, m_Tex);

		glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, dataFmt, GL_UNSIGNED_BYTE, pixels);

		if (generateMipmaps) {
			glGenerateMipmap(GL_TEXTURE_2D);
		}

		m_HasMips = generateMipmaps;
		ApplySamplerParams();

		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(pixels);

		m_Width = w; m_Height = h; m_Channels = n;
		return true;
	}

	void Texture2D::SetSampler(Filter filter, Wrap u, Wrap v) {
		m_Filter = filter;
		m_WrapU = u;
		m_WrapV = v;
		if (IsValid()) ApplySamplerParams();
	}

	void Texture2D::SetFilter(Filter filter) {
		m_Filter = filter;
		if (IsValid()) ApplySamplerParams();
	}

	void Texture2D::SetWrapU(Wrap u) {
		m_WrapU = u;
		if (IsValid()) ApplySamplerParams();
	}

	void Texture2D::SetWrapV(Wrap v) {
		m_WrapV = v;
		if (IsValid()) ApplySamplerParams();
	}

	std::unique_ptr<ImageData> Texture2D::GetImageData() const {
		AIM_ASSERT(IsValid(), AxiomErrorCode::InvalidHandle, "Texture isn't valid!");

		int w = 0, h = 0;
		glBindTexture(GL_TEXTURE_2D, m_Tex);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);


		AIM_ASSERT(w > 0 && h > 0, AxiomErrorCode::OutOfBounds, "Texture width or height isn't valid!");

		std::vector<unsigned char> pixels((size_t)w * (size_t)h * 4);

		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		GLsizei bufSize = (GLsizei)pixels.size();
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		return std::make_unique<ImageData>(ImageData(w, h, std::move(pixels)));
	}

	void Texture2D::ApplySamplerParams() const {
		if (!m_Tex) return;
		glBindTexture(GL_TEXTURE_2D, m_Tex);

		auto wrapU = static_cast<GLint>(m_WrapU);
		auto wrapV = static_cast<GLint>(m_WrapV);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapU);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapV);
		if (wrapU == GL_CLAMP_TO_BORDER || wrapV == GL_CLAMP_TO_BORDER) {
			const float border[4] = { 0,0,0,0 };
			glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
		}


		GLint mag = (m_Filter == Filter::Point) ? GL_NEAREST : GL_LINEAR;
		GLint minNoMip = (m_Filter == Filter::Point) ? GL_NEAREST : GL_LINEAR;
		GLint minWithMip = GL_NEAREST_MIPMAP_NEAREST;

		switch (m_Filter) {
		case Filter::Point:
			minWithMip = GL_NEAREST_MIPMAP_NEAREST; break;
		case Filter::Bilinear:
			minWithMip = GL_LINEAR_MIPMAP_NEAREST; break;
		case Filter::Trilinear:
		case Filter::Anisotropic:
			minWithMip = GL_LINEAR_MIPMAP_LINEAR;  break;
		}

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_HasMips ? minWithMip : minNoMip);


		if (m_Filter == Filter::Anisotropic) {
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
			GLfloat maxAniso = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::max(2.0f, maxAniso));
#endif
		}
		else {
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
#endif
		}

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void Texture2D::Submit(uint8_t unit) const {
		if (!IsValid()) return;
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, m_Tex);
	}
}