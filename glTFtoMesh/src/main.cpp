#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/Deserialize.h"
#include "meshoptimizer.h"
#include "mikktspace.h"

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

#include <string>
#include <fstream>
#include <sstream>

#include "mesh_work.h"

#define private public
#include "sl12/resource_mesh.h"
#undef private


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

}

struct ToolOptions
{
	std::string		inputFileName = "";
	std::string		inputPath = "";
	std::string		outputFilePath = "";
	std::string		outputTexPath = "";

	bool			mergeFlag = true;
	bool			optimizeFlag = true;
	bool			meshletFlag = false;
};	// struct ToolOptions

int main(int argv, char* argc[])
{
	if (argv == 1)
	{
		// display help.
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

	auto mesh_work = std::make_unique<MeshWork>();
	if (!mesh_work->ReadGLTFMesh(options.inputPath, options.inputFileName))
	{
		return -1;
	}

	if (options.mergeFlag)
	{
		if (mesh_work->MergeSubmesh() == 0)
		{
			return -1;
		}
	}

	if (options.optimizeFlag)
	{
		mesh_work->OptimizeSubmesh();
	}

	if (options.meshletFlag)
	{
		mesh_work->BuildMeshlets();
	}

	// output textures.
	for (auto&& tex : mesh_work->GetTextures())
	{
		std::fstream ofs(options.outputTexPath + tex->GetName(), std::ios::out | std::ios::binary);
		ofs.write((const char*)tex->GetBinary().data(), tex->GetBinary().size());
	}

	// output binary.
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
		sl12::ResourceMeshMaterial out_mat;
		out_mat.name_ = mat->GetName();
		out_mat.textureNames_.push_back(mat->GetTextrues()[MaterialWork::TextureKind::BaseColor]);
		out_mat.textureNames_.push_back(mat->GetTextrues()[MaterialWork::TextureKind::Normal]);
		out_mat.textureNames_.push_back(mat->GetTextrues()[MaterialWork::TextureKind::ORM]);
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
			out_sub.meshlets_.push_back(m);
		}

		out_resource->submeshes_.push_back(out_sub);
	}

	{
		std::fstream ofs(options.outputFilePath, std::ios::out | std::ios::binary);
		cereal::BinaryOutputArchive ar(ofs);
		ar(cereal::make_nvp("mesh", *out_resource));
	}

	return 0;
}


//	EOF
