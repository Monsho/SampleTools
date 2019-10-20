#pragma once

#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/Deserialize.h"
#include "meshoptimizer.h"
#include "mikktspace.h"
#include <DirectXMath.h>


struct Vertex
{
	DirectX::XMFLOAT3	pos;
	DirectX::XMFLOAT3	normal;
	DirectX::XMFLOAT4	tangent;
	DirectX::XMFLOAT2	uv;
};	// struct Vertex

struct BoundSphere
{
	DirectX::XMFLOAT3	center;
	float				radius;
};

struct BoundBox
{
	DirectX::XMFLOAT3	aabbMin;
	DirectX::XMFLOAT3	aabbMax;
};

struct Meshlet
{
	uint32_t				indexOffset;
	uint32_t				indexCount;
	BoundSphere				boundingSphere;
	BoundBox				boundingBox;
};	// struct Meshlet

class SubmeshWork
{
	friend class MeshWork;

public:
	SubmeshWork()
	{}
	~SubmeshWork()
	{}

	int GetMaterialIndex() const
	{
		return materialIndex_;
	}
	const std::vector<Vertex>& GetVertexBuffer() const
	{
		return vertexBuffer_;
	}
	const std::vector<uint32_t>& GetIndexBuffer() const
	{
		return indexBuffer_;
	}
	const BoundSphere& GetBoundingSphere() const
	{
		return boundingSphere_;
	}
	const BoundBox& GetBoundingBox() const
	{
		return boundingBox_;
	}
	const std::vector<Meshlet>& GetMeshlets() const
	{
		return meshlets_;
	}

private:
	int						materialIndex_;
	std::vector<Vertex>		vertexBuffer_;
	std::vector<uint32_t>	indexBuffer_;
	BoundSphere				boundingSphere_;
	BoundBox				boundingBox_;

	std::vector<Meshlet>	meshlets_;
	std::vector<uint32_t>	meshletIndexBuffer_;
};	// class SubmeshWork

class MaterialWork
{
	friend class MeshWork;

public:
	struct TextureKind
	{
		enum {
			BaseColor,
			Normal,
			ORM,

			Max
		};
	};	// struct TextureType

public:
	MaterialWork()
	{}
	~MaterialWork()
	{}

	const std::string& GetName() const
	{
		return name_;
	}
	const std::string* GetTextrues() const
	{
		return textures_;
	}
	bool IsOpaque() const
	{
		return isOpaque_;
	}

private:
	std::string		name_;
	std::string		textures_[TextureKind::Max];
	bool			isOpaque_;
};	// class MaterialWork

class TextureWork
{
	friend class MeshWork;

public:
	TextureWork()
	{}
	~TextureWork()
	{}

	const std::string& GetName() const
	{
		return name_;
	}
	const std::vector<uint8_t>& GetBinary() const
	{
		return binary_;
	}

private:
	std::string				name_;
	std::vector<uint8_t>	binary_;
};	// class TextureWork

class MeshWork
{
public:
	MeshWork()
	{}
	~MeshWork()
	{}

	bool ReadGLTFMesh(const std::string& inputPath, const std::string& inputFile);

	size_t MergeSubmesh();

	void OptimizeSubmesh();

	void BuildMeshlets();

	const std::vector<std::unique_ptr<MaterialWork>>& GetMaterials() const
	{
		return materials_;
	}
	const std::vector<std::unique_ptr<SubmeshWork>>& GetSubmeshes() const
	{
		return submeshes_;
	}
	const std::vector<std::unique_ptr<TextureWork>>& GetTextures() const
	{
		return textures_;
	}
	const BoundSphere& GetBoundingSphere() const
	{
		return boundingSphere_;
	}
	const BoundBox& GetBoundingBox() const
	{
		return boundingBox_;
	}

private:
	std::string									sourceFilePath_;
	std::vector<std::unique_ptr<MaterialWork>>	materials_;
	std::vector<std::unique_ptr<SubmeshWork>>	submeshes_;
	std::vector<std::unique_ptr<TextureWork>>	textures_;

	BoundSphere				boundingSphere_;
	BoundBox				boundingBox_;
};	// class MeshWork

//	EOF
