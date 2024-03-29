﻿#include <algorithm>
#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/Deserialize.h"
#include "meshoptimizer.h"
#include "mikktspace.h"

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

#include <DirectXTex.h>

#include <string>
#include <fstream>
#include <sstream>

#include "mesh_work.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../External/stb/stb_image.h"

#define private public
#include "sl12/resource_mesh.h"
#undef private

#define NOMINMAX
#include <windows.h>
#include <imagehlp.h>
#pragma comment(lib, "imagehlp.lib")


using namespace Microsoft::glTF;

namespace
{
	std::string ConvYenToSlash(const std::string& path)
	{
		std::string ret;
		ret.reserve(path.length() + 1);
		for (auto&& it : path)
		{
			ret += (it == '\\') ? '/' : it;
		}
		return ret;
	}

	std::string ConvSlashToYen(const std::string& path)
	{
		std::string ret;
		ret.reserve(path.length() + 1);
		for (auto&& it : path)
		{
			ret += (it == '/') ? '\\' : it;
		}
		return ret;
	}

	std::string GetExtent(const std::string& filename)
	{
		std::string ret;
		auto pos = filename.rfind('.');
		if (pos != std::string::npos)
		{
			ret = filename.data() + pos;
		}
		return ret;
	}

	std::string GetFileName(const std::string& filename)
	{
		std::string ret = filename;
		auto pos = filename.rfind('.');
		if (pos != std::string::npos)
		{
			ret.erase(pos);
		}
		return ret;
	}

	std::string GetPath(const std::string& filename)
	{
		std::string ret = "./";
		auto pos = filename.rfind('/');
		if (pos != std::string::npos)
		{
			ret = filename.substr(0, pos + 1);
		}
		return ret;
	}

	std::string GetTextureKind(const std::string& filename)
	{
		std::string name = GetFileName(filename);
		size_t pos = name.rfind(".");
		std::string ret;
		if (pos != std::string::npos)
		{
			ret = name.data() + pos + 1;
		}
		return ret;
	}
}

struct ToolOptions
{
	std::string		inputFileName = "";
	std::string		inputPath = "";
	std::string		outputFilePath = "";
	std::string		outputTexPath = "";

	bool			textureDDS = true;
	bool			compressBC7 = false;
	bool			mergeFlag = true;
	bool			optimizeFlag = true;
	bool			meshletFlag = false;
};	// struct ToolOptions

void DisplayHelp()
{
	fprintf(stdout, "glTFtoMesh : Convert glTF format to sl12 mesh format.\n");
	fprintf(stdout, "options:\n");
	fprintf(stdout, "    -i <file_path>  : input glTf(.glb) file path.\n");
	fprintf(stdout, "    -o <file_path>  : output sl12 mesh(.rmesh) file path.\n");
	fprintf(stdout, "    -to <directory> : output texture file directory.\n");
	fprintf(stdout, "    -dds <0/1>      : change texture format png to dds, or not. (default: 1)\n");
	fprintf(stdout, "    -bc7 <0/1>      : if 1, use bc7 compression for a part of dds. if 0, use bc3. (default: 0)\n");
	fprintf(stdout, "    -merge <0/1>    : merge submeshes have same material. (default: 1)\n");
	fprintf(stdout, "    -opt <0/1>      : optimize mesh. (default: 1)\n");
	fprintf(stdout, "    -let <0/1>      : create meshlets. (default: 0)\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "example:\n");
	fprintf(stdout, "    glTFtoMesh.exe -i \"D:/input/sample.glb\" -o \"D:/output/sample.rmesh\" -to \"D:/output/textures/\" -let 1\n");
}

bool ConvertToDDS(TextureWork* pTex, const std::string& outputFilePath, bool isSrgb, bool isNormal, bool isBC7)
{
	// read png image.
	int width, height, bpp;
	auto pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(pTex->GetBinary().data()), static_cast<int>(pTex->GetBinary().size()), &width, &height, &bpp, 0);
	if (!pixels || (bpp != 3 && bpp != 4))
	{
		return false;
	}

	// convert to DirectX image.
	std::unique_ptr<DirectX::ScratchImage> image(new DirectX::ScratchImage());
	auto hr = image->Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	bool has_alpha = false;
	if (FAILED(hr))
	{
		return false;
	}
	if (bpp == 3)
	{
		auto src = pixels;
		auto dst = image->GetPixels();
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < height; x++)
			{
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 0xff;
				src += 3;
				dst += 4;
			}
		}
	}
	else
	{
		auto src = pixels;
		auto dst = image->GetPixels();
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < height; x++)
			{
				has_alpha = has_alpha || (src[3] < 0xff);
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = src[3];
				src += 4;
				dst += 4;
			}
		}
	}
	stbi_image_free(pixels);

	// generate full mips.
	std::unique_ptr<DirectX::ScratchImage> mipped_image(new DirectX::ScratchImage());
	hr = DirectX::GenerateMipMaps(
		*image->GetImage(0, 0, 0),
		DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_FORCE_NON_WIC,
		0,
		*mipped_image);
	image.swap(mipped_image);

	// compress.
	DXGI_FORMAT compress_format = (isSrgb) ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
	if (has_alpha || isNormal)
	{
		compress_format = (isBC7)
			? ((isSrgb) ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM)
			: ((isSrgb) ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM);
	}
	DirectX::TEX_COMPRESS_FLAGS comp_flag = DirectX::TEX_COMPRESS_PARALLEL;
	if (isSrgb)
	{
		comp_flag |= DirectX::TEX_COMPRESS_SRGB_OUT;
	}
	std::unique_ptr<DirectX::ScratchImage> comp_image(new DirectX::ScratchImage());
	hr = DirectX::Compress(
		image->GetImages(),
		image->GetImageCount(),
		image->GetMetadata(),
		compress_format,
		comp_flag,
		DirectX::TEX_THRESHOLD_DEFAULT,
		*comp_image);
	if (FAILED(hr))
	{
		return false;
	}
	image.swap(comp_image);

	size_t len;
	mbstowcs_s(&len, nullptr, 0, outputFilePath.c_str(), 0);
	std::wstring of;
	of.resize(len + 1);
	mbstowcs_s(&len, (wchar_t*)of.data(), of.length(), outputFilePath.c_str(), of.length());
	hr = DirectX::SaveToDDSFile(
		image->GetImages(),
		image->GetImageCount(),
		image->GetMetadata(),
		DirectX::DDS_FLAGS_NONE,
		of.c_str());
	if (FAILED(hr))
	{
		return false;
	}

	return true;
}

int main(int argv, char* argc[])
{
	if (argv == 1)
	{
		// display help.
		DisplayHelp();
		return 0;
	}

	// get options.
	ToolOptions options;
	for (int i = 1; i < argv; i++)
	{
		std::string op = argc[i];
		if (op[0] == '-' || op[0] == '/')
		{
			if (op == "-i" || op == "/i")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.inputPath = ConvYenToSlash(argc[++i]);
				auto slash = options.inputPath.rfind('/');
				if (slash == std::string::npos)
				{
					options.inputFileName = options.inputPath;
					options.inputPath = "./";
				}
				else
				{
					options.inputFileName = &options.inputPath.data()[slash + 1];
					options.inputPath = options.inputPath.erase(slash + 1, std::string::npos);
				}
			}
			else if (op == "-o" || op == "/o")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.outputFilePath = argc[++i];
			}
			else if (op == "-to" || op == "/to")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.outputTexPath = argc[++i];
			}
			else if (op == "-dds" || op == "/dds")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.textureDDS = std::stoi(argc[++i]);
			}
			else if (op == "-bc7" || op == "/bc7")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.compressBC7 = std::stoi(argc[++i]);
			}
			else if (op == "-merge" || op == "/merge")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.mergeFlag = std::stoi(argc[++i]);
			}
			else if (op == "-opt" || op == "/opt")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.optimizeFlag = std::stoi(argc[++i]);
			}
			else if (op == "-let" || op == "/let")
			{
				if (i == argv - 1)
				{
					fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
					return -1;
				}
				options.meshletFlag = std::stoi(argc[++i]);
			}
			else
			{
				fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
				return -1;
			}
		}
		else
		{
			fprintf(stderr, "invalid argument. (%s)\n", op.c_str());
			return -1;
		}
	}

	if (options.inputFileName.empty() || options.inputPath.empty())
	{
		fprintf(stderr, "invalid input file name.\n");
		return -1;
	}
	if (options.outputFilePath.empty())
	{
		fprintf(stderr, "invalid output file name.\n");
		return -1;
	}
	if (options.outputTexPath.empty())
	{
		options.outputTexPath = GetPath(ConvYenToSlash(options.outputFilePath));
	}
	else
	{
		options.outputTexPath = ConvYenToSlash(options.outputTexPath);
		if (options.outputTexPath[options.outputTexPath.length() - 1] != '/')
		{
			options.outputTexPath += '/';
		}
	}

	{
		auto outDir = GetPath(ConvYenToSlash(options.outputFilePath));
		MakeSureDirectoryPathExists(ConvSlashToYen(outDir).c_str());
		MakeSureDirectoryPathExists(ConvSlashToYen(options.outputTexPath).c_str());
	}

	fprintf(stdout, "read glTF mesh. (%s)\n", options.inputFileName.c_str());
	auto mesh_work = std::make_unique<MeshWork>();
	if (!mesh_work->ReadGLTFMesh(options.inputPath, options.inputFileName))
	{
		fprintf(stderr, "failed to read glTF mesh. (%s)\n", options.inputFileName.c_str());
		return -1;
	}

	if (options.mergeFlag)
	{
		fprintf(stdout, "merge submeshes.\n");
		if (mesh_work->MergeSubmesh() == 0)
		{
			fprintf(stderr, "failed to merge submeshes.\n");
			return -1;
		}
	}

	if (options.optimizeFlag)
	{
		fprintf(stdout, "optimize mesh.\n");
		mesh_work->OptimizeSubmesh();
	}

	if (options.meshletFlag)
	{
		fprintf(stdout, "build meshlets.\n");
		mesh_work->BuildMeshlets();
	}

	// output textures.
	if (options.textureDDS)
	{
		if (!mesh_work->GetTextures().empty())
		{
			fprintf(stdout, "output DDS textures.\n");
			HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			for (auto&& tex : mesh_work->GetTextures())
			{
				std::string name = GetFileName(tex->GetName()) + ".dds";
				std::string kind = GetTextureKind(tex->GetName());
				fprintf(stdout, "writing %s texture... (kind: %s)\n", name.c_str(), kind.c_str());
				if (!ConvertToDDS(tex.get(), options.outputTexPath + name, kind == "bc", kind == "n", options.compressBC7))
				{
					fprintf(stderr, "failed to write %s texture...\n", name.c_str());
					return -1;
				}
			}

			CoUninitialize();
			fprintf(stdout, "complete to output DDS textures.\n");
		}
	}
	else
	{
		fprintf(stdout, "output PNG textures.\n");
		for (auto&& tex : mesh_work->GetTextures())
		{
			fprintf(stdout, "writing %s texture...\n", tex->GetName().c_str());
			std::fstream ofs(options.outputTexPath + tex->GetName(), std::ios::out | std::ios::binary);
			ofs.write((const char*)tex->GetBinary().data(), tex->GetBinary().size());
		}
		fprintf(stdout, "complete to output PNG textures.\n");
	}

	auto PNGtoDDS = [](const std::string& filename)
	{
		std::string ret = filename;
		auto pos = ret.rfind(".png");
		if (pos != std::string::npos)
		{
			ret.erase(pos);
			ret += ".dds";
		}
		return ret;
	};

	// output binary.
	fprintf(stdout, "output rmesh binary.\n");
	auto out_resource = std::make_unique<sl12::ResourceMesh>();
	out_resource->boundingSphere_.centerX = mesh_work->GetBoundingSphere().center.x;
	out_resource->boundingSphere_.centerY = mesh_work->GetBoundingSphere().center.y;
	out_resource->boundingSphere_.centerZ = mesh_work->GetBoundingSphere().center.z;
	out_resource->boundingSphere_.radius = mesh_work->GetBoundingSphere().radius;
	out_resource->boundingBox_.minX = mesh_work->GetBoundingBox().aabbMin.x;
	out_resource->boundingBox_.minY = mesh_work->GetBoundingBox().aabbMin.y;
	out_resource->boundingBox_.minZ = mesh_work->GetBoundingBox().aabbMin.z;
	out_resource->boundingBox_.maxX = mesh_work->GetBoundingBox().aabbMax.x;
	out_resource->boundingBox_.maxY = mesh_work->GetBoundingBox().aabbMax.y;
	out_resource->boundingBox_.maxZ = mesh_work->GetBoundingBox().aabbMax.z;
	for (auto&& mat : mesh_work->GetMaterials())
	{
		auto bcName = mat->GetTextrues()[MaterialWork::TextureKind::BaseColor];
		auto nName = mat->GetTextrues()[MaterialWork::TextureKind::Normal];
		auto ormName = mat->GetTextrues()[MaterialWork::TextureKind::ORM];
		if (options.textureDDS)
		{
			bcName = PNGtoDDS(bcName);
			nName = PNGtoDDS(nName);
			ormName = PNGtoDDS(ormName);
		}

		sl12::ResourceMeshMaterial out_mat;
		out_mat.name_ = mat->GetName();
		out_mat.textureNames_.push_back(bcName);
		out_mat.textureNames_.push_back(nName);
		out_mat.textureNames_.push_back(ormName);
		out_mat.isOpaque_ = mat->IsOpaque();
		out_resource->materials_.push_back(out_mat);
	}
	uint32_t vb_offset = 0;
	uint32_t ib_offset = 0;
	uint32_t pb_offset = 0;
	uint32_t vib_offset = 0;
	for (auto&& submesh : mesh_work->GetSubmeshes())
	{
		sl12::ResourceMeshSubmesh out_sub;
		out_sub.materialIndex_ = submesh->GetMaterialIndex();

		auto&& src_vb = submesh->GetVertexBuffer();
		auto&& src_ib = submesh->GetIndexBuffer();
		auto&& src_pb = submesh->GetPackedPrimitive();
		auto&& src_vib = submesh->GetVertexIndexBuffer();
		std::vector<float> vbp, vbn, vbt, vbu;
		vbp.resize(3 * src_vb.size());
		vbn.resize(3 * src_vb.size());
		vbt.resize(4 * src_vb.size());
		vbu.resize(2 * src_vb.size());
		for (size_t i = 0; i < src_vb.size(); i++)
		{
			memcpy(&vbp[i * 3], &src_vb[i].pos, sizeof(float) * 3);
			memcpy(&vbn[i * 3], &src_vb[i].normal, sizeof(float) * 3);
			memcpy(&vbt[i * 4], &src_vb[i].tangent, sizeof(float) * 4);
			memcpy(&vbu[i * 2], &src_vb[i].uv, sizeof(float) * 2);
		}

		auto CopyBuffer = [](std::vector<sl12::u8>& dst, const void* pData, size_t dataSize)
		{
			auto cs = dst.size();
			dst.resize(cs + dataSize);
			memcpy(dst.data() + cs, pData, dataSize);
		};
		CopyBuffer(out_resource->vbPosition_,  vbp.data(), sizeof(float) * vbp.size());
		CopyBuffer(out_resource->vbNormal_,    vbn.data(), sizeof(float) * vbn.size());
		CopyBuffer(out_resource->vbTangent_,   vbt.data(), sizeof(float) * vbt.size());
		CopyBuffer(out_resource->vbTexcoord_,  vbu.data(), sizeof(float) * vbu.size());
		CopyBuffer(out_resource->indexBuffer_, src_ib.data(), sizeof(uint32_t)* src_ib.size());
		CopyBuffer(out_resource->meshletPackedPrimitive_, src_pb.data(), sizeof(uint32_t)* src_pb.size());
		CopyBuffer(out_resource->meshletVertexIndex_, src_vib.data(), sizeof(float) * src_vib.size());

		out_sub.vertexOffset_ = vb_offset;
		out_sub.vertexCount_ = (uint32_t)src_vb.size();
		out_sub.indexOffset_ = ib_offset;
		out_sub.indexCount_ = (uint32_t)src_ib.size();
		out_sub.meshletPrimitiveOffset_ = pb_offset;
		out_sub.meshletPrimitiveCount_ = (uint32_t)src_pb.size();
		out_sub.meshletVertexIndexOffset_ = vib_offset;
		out_sub.meshletVertexIndexCount_ = (uint32_t)src_vib.size();
		vb_offset += out_sub.vertexCount_;
		ib_offset += out_sub.indexCount_;
		pb_offset += out_sub.meshletPrimitiveCount_;
		vib_offset += out_sub.meshletVertexIndexCount_;

		out_sub.boundingSphere_.centerX = submesh->GetBoundingSphere().center.x;
		out_sub.boundingSphere_.centerY = submesh->GetBoundingSphere().center.y;
		out_sub.boundingSphere_.centerZ = submesh->GetBoundingSphere().center.z;
		out_sub.boundingSphere_.radius = submesh->GetBoundingSphere().radius;
		out_sub.boundingBox_.minX = submesh->GetBoundingBox().aabbMin.x;
		out_sub.boundingBox_.minY = submesh->GetBoundingBox().aabbMin.y;
		out_sub.boundingBox_.minZ = submesh->GetBoundingBox().aabbMin.z;
		out_sub.boundingBox_.maxX = submesh->GetBoundingBox().aabbMax.x;
		out_sub.boundingBox_.maxY = submesh->GetBoundingBox().aabbMax.y;
		out_sub.boundingBox_.maxZ = submesh->GetBoundingBox().aabbMax.z;

		for (auto&& meshlet : submesh->GetMeshlets())
		{
			sl12::ResourceMeshMeshlet m;
			m.indexOffset_ = meshlet.indexOffset;
			m.indexCount_ = meshlet.indexCount;
			m.primitiveOffset_ = meshlet.primitiveOffset;
			m.primitiveCount_ = meshlet.primitiveCount;
			m.vertexIndexOffset_ = meshlet.vertexIndexOffset;
			m.vertexIndexCount_ = meshlet.vertexIndexCount;
			m.boundingSphere_.centerX = meshlet.boundingSphere.center.x;
			m.boundingSphere_.centerY = meshlet.boundingSphere.center.y;
			m.boundingSphere_.centerZ = meshlet.boundingSphere.center.z;
			m.boundingSphere_.radius = meshlet.boundingSphere.radius;
			m.boundingBox_.minX = meshlet.boundingBox.aabbMin.x;
			m.boundingBox_.minY = meshlet.boundingBox.aabbMin.y;
			m.boundingBox_.minZ = meshlet.boundingBox.aabbMin.z;
			m.boundingBox_.maxX = meshlet.boundingBox.aabbMax.x;
			m.boundingBox_.maxY = meshlet.boundingBox.aabbMax.y;
			m.boundingBox_.maxZ = meshlet.boundingBox.aabbMax.z;
			m.cone_.apexX = meshlet.cone.apex.x;
			m.cone_.apexY = meshlet.cone.apex.y;
			m.cone_.apexZ = meshlet.cone.apex.z;
			m.cone_.axisX = meshlet.cone.axis.x;
			m.cone_.axisY = meshlet.cone.axis.y;
			m.cone_.axisZ = meshlet.cone.axis.z;
			m.cone_.cutoff = meshlet.cone.cutoff;
			out_sub.meshlets_.push_back(m);
		}

		out_resource->submeshes_.push_back(out_sub);
	}

	{
		std::fstream ofs(options.outputFilePath, std::ios::out | std::ios::binary);
		cereal::BinaryOutputArchive ar(ofs);
		ar(cereal::make_nvp("mesh", *out_resource));
	}

	fprintf(stdout, "convert succeeded!!.\n");

	return 0;
}


//	EOF
