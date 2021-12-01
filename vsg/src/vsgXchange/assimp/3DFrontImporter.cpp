#include "3DFrontImporter.h"

#include <assimp/scene.h>
#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <filesystem>
#include <cstring>


namespace fs = std::filesystem;

static void DeepCopyAiMesh(aiMesh* source, aiMesh*& target)
{
    target = new aiMesh;
    *target = *source;
    if (source->mNumVertices > 0)
    {
        target->mVertices = new aiVector3D[source->mNumVertices];
        std::memcpy(target->mVertices, source->mVertices, target->mNumVertices * sizeof(aiVector3D));
        if (source->mNormals != nullptr)
        {
            target->mNormals = new aiVector3D[source->mNumVertices];
            std::memcpy(target->mNormals, source->mNormals, target->mNumVertices * sizeof(aiVector3D));
        }
        if (source->mTangents != nullptr)
        {
            target->mTangents = new aiVector3D[source->mNumVertices];
            std::memcpy(target->mTangents, source->mTangents, target->mNumVertices * sizeof(aiVector3D));
        }
        if (source->mBitangents != nullptr)
        {
            target->mBitangents = new aiVector3D[source->mNumVertices];
            std::memcpy(target->mBitangents, source->mBitangents, target->mNumVertices * sizeof(aiVector3D));
        }
        for (int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; i++)
        {
            if (source->mTextureCoords[i] != nullptr)
            {
                target->mTextureCoords[i] = new aiVector3D[source->mNumVertices];
                std::memcpy(target->mTextureCoords[i], source->mTextureCoords[i], target->mNumVertices * sizeof(aiVector3D));
            }
        }
    }
    target->mFaces = new aiFace[source->mNumFaces];
    std::memcpy(target->mFaces, source->mFaces, target->mNumFaces * sizeof(aiFace));
    for (int face_index = 0; face_index < target->mNumFaces; face_index++)
    {
        auto& face = target->mFaces[face_index];
        face.mIndices = new unsigned[face.mNumIndices];
        std::memcpy(face.mIndices, source->mFaces[face_index].mIndices, face.mNumIndices * sizeof(unsigned));
    }
    // TODO: copy remaining data
}

static const aiImporterDesc desc = {
    "3D-FRONT scene importer",
    "",
    "",
    "",
    aiImporterFlags_SupportTextFlavour,
    0,
    0,
    0,
    0,
    "json"
};

bool AI3DFrontImporter::CanRead(const std::string& pFile, Assimp::IOSystem* pIOHandler, bool checkSig) const
{
    return SimpleExtensionCheck(pFile, "json");
}
const aiImporterDesc* AI3DFrontImporter::GetInfo() const
{
    return &desc;
}
void AI3DFrontImporter::InternReadFile(const std::string& pFile, aiScene* pScene, Assimp::IOSystem* pIOHandler)
{
    std::ifstream scene_file(pFile);
    if (!scene_file)
    {
        throw DeadlyImportError("Failed to open file " + pFile + ".");
    }
    nlohmann::json scene_json;
    scene_file >> scene_json;

    std::vector<fs::path> furniture_directories;
    std::vector<fs::path> texture_directories;
    FindDataDirectories(pFile, texture_directories, furniture_directories);

    // load materials
    std::unordered_map<std::string, uint32_t> material_id_to_index_map;
    std::vector<float> material_uv_rotations;
    const auto& materials = scene_json["material"];
    pScene->mNumMaterials = materials.size();
    if (pScene->mNumMaterials > 0)
    {
        pScene->mMaterials = new aiMaterial*[pScene->mNumMaterials];
        uint32_t material_index = 0;
        for (const auto& raw_material : materials)
        {
            pScene->mMaterials[material_index] = new aiMaterial;
            auto& ai_material = pScene->mMaterials[material_index];

            bool use_color;
            if (const auto& iterator = raw_material.find("use_color"); iterator != raw_material.end())
            {
                use_color = iterator.value();
            }
            else
            {
                use_color = std::string(raw_material["texture"]).empty();
            }
            //if (use_color)
            {
                const auto& raw_color = raw_material["color"];
                aiColor4D color(static_cast<float>(raw_color[0]) / 255.f, static_cast<float>(raw_color[1]) / 255.f,
                                static_cast<float>(raw_color[2]) / 255.f,
                                static_cast<float>(raw_color[3]) / 255.f);
                ai_material->AddProperty(&color, 1, AI_MATKEY_COLOR_DIFFUSE);
            }

            const auto& material_id = std::string(raw_material["jid"]);
            if (!material_id.empty())
            {
                for (const auto& texture_directory : texture_directories)
                {
                    fs::path texture_path = texture_directory / fs::path(std::string(material_id));
                    if (!fs::exists(texture_path))
                    {
                        continue;
                    }
                    // add texture
                    aiString text_path_str((texture_path / fs::path("texture.png")).string());
                    const int uvwIndex = 0;
                    ai_material->AddProperty(&text_path_str, AI_MATKEY_TEXTURE_DIFFUSE(0));
                    ai_material->AddProperty(&uvwIndex, 1, AI_MATKEY_UVWSRC_DIFFUSE(0));
                    const int address_mode = aiTextureMapMode_Wrap;
                    ai_material->AddProperty<int>(&address_mode, 1, AI_MATKEY_MAPPINGMODE_U_DIFFUSE(0));
                    ai_material->AddProperty<int>(&address_mode, 1, AI_MATKEY_MAPPINGMODE_V_DIFFUSE(0));
                    // we have our texture and don't need to check the other directories
                    break;
                }
            }
            float uv_rotation = 0;
            if (const auto& iterator = raw_material.find("UVTransform"); iterator != raw_material.end())
            {
                // linearized 3x3 matrix
                const auto& raw_uv_transform = iterator.value();
                // TODO: https://math.stackexchange.com/questions/237369/given-this-transformation-matrix-how-do-i-decompose-it-into-translation-rotati/3554913
                aiUVTransform ai_uv_transform;
                ai_uv_transform.mTranslation = aiVector2D(raw_uv_transform[6], raw_uv_transform[7]);
                auto column_0 = aiVector2D(raw_uv_transform[0], raw_uv_transform[1]);
                float column_0_length = column_0.Length();
                ai_uv_transform.mScaling.x = aiVector2D(raw_uv_transform[0], raw_uv_transform[1]).Length();
                ai_uv_transform.mScaling.y = aiVector2D(raw_uv_transform[3], raw_uv_transform[4]).Length();
                ai_uv_transform.mRotation = acos(column_0.x / column_0_length);
                // need this because rotations are not stored correctly by assimp
                uv_rotation = ai_uv_transform.mRotation;
                ai_material->AddProperty(&ai_uv_transform, 1, AI_MATKEY_UVTRANSFORM_DIFFUSE(0));
            }
            material_uv_rotations.push_back(uv_rotation);

            material_id_to_index_map[raw_material["uid"]] = material_index++;
        }
    }

    // load base meshes
    std::unordered_map<std::string, std::vector<uint32_t>> model_uid_to_mesh_indices_map;
    const auto& room_meshes = scene_json["mesh"];
    pScene->mNumMeshes = room_meshes.size();
    if (pScene->mNumMeshes > 0)
    {
        pScene->mMeshes = new aiMesh*[pScene->mNumMeshes];
        uint32_t mesh_index = 0;
        for (const auto& raw_mesh : room_meshes)
        {
            pScene->mMeshes[mesh_index] = new aiMesh;
            auto& ai_mesh = pScene->mMeshes[mesh_index];
            ai_mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
            ai_mesh->mNumUVComponents[0] = 2;

            ai_mesh->mMaterialIndex = material_id_to_index_map[raw_mesh["material"]];
            const auto& obj_type = std::string(raw_mesh["type"]);
            if (obj_type.find("Ceiling") != std::string::npos)
            {
                auto& material = pScene->mMaterials[ai_mesh->mMaterialIndex];
                aiColor3D emissive_color(_ceiling_light_strength);
                //material->AddProperty(&emissive_color, 1, AI_MATKEY_COLOR_EMISSIVE);
            }         

            // parse vertices, normals and tex coords
            const auto& raw_vertices = raw_mesh["xyz"];
            ai_mesh->mNumVertices = raw_vertices.size() / 3;
            if (ai_mesh->mNumVertices > 0)
            {
                const auto& raw_normals = raw_mesh["normal"];
                const auto& raw_tex_coords = raw_mesh["uv"];

                ai_mesh->mVertices = new aiVector3D[ai_mesh->mNumVertices];
                ai_mesh->mNormals = new aiVector3D[ai_mesh->mNumVertices];
                ai_mesh->mTextureCoords[0] = new aiVector3D[ai_mesh->mNumVertices];
                for (int i = 0; i < ai_mesh->mNumVertices; i++)
                {
                    int x_index = i * 3;
                    int y_index = x_index + 1;
                    int z_index = x_index + 2;
                    int u_index = i * 2;
                    int v_index = u_index + 1;

                    ai_mesh->mVertices[i] = aiVector3D(raw_vertices[x_index], raw_vertices[y_index], raw_vertices[z_index]);
                    ai_mesh->mNormals[i] = aiVector3D(raw_normals[x_index], raw_normals[y_index], raw_normals[z_index]);

                    {
                        // somehow this prevents the floor from disappering
                        // the offset is negated by the child node transformation in the scene parsing part
                        ai_mesh->mVertices[i].y += 5;
                    }
                    // small offset to prevent z-fighting with objects on the floor
                    ai_mesh->mVertices[i].y += 0.0001;

                    ai_mesh->mTextureCoords[0][i] = aiVector3D(raw_tex_coords[u_index], raw_tex_coords[v_index], 0);

                    // transform uv coords based on material
                    const aiMaterial* material = pScene->mMaterials[ai_mesh->mMaterialIndex];
                    aiUVTransform ai_uv_transform;
                    if (material->Get(AI_MATKEY_UVTRANSFORM_DIFFUSE(0), ai_uv_transform) == AI_SUCCESS)
                    {
                        ai_uv_transform.mRotation = material_uv_rotations[ai_mesh->mMaterialIndex];

                        auto& tex_coord = ai_mesh->mTextureCoords[0][i];
                        tex_coord.x *= ai_uv_transform.mScaling.x;
                        tex_coord.y *= ai_uv_transform.mScaling.y;
                        aiMatrix3x3 rotation_matrix;
                        aiMatrix3x3::RotationZ(ai_uv_transform.mRotation, rotation_matrix);
                        tex_coord = rotation_matrix * tex_coord;
                        tex_coord.x += ai_uv_transform.mTranslation.x;
                        tex_coord.y += ai_uv_transform.mTranslation.y;
                    } 
                }
            }

            // parse indices
            const auto& raw_indices = raw_mesh["faces"];
            ai_mesh->mNumFaces = raw_indices.size() / 3;
            if (ai_mesh->mNumFaces > 0)
            {
                ai_mesh->mFaces = new aiFace[ai_mesh->mNumFaces];
                for (int i = 0; i < ai_mesh->mNumFaces; i++)
                {
                    int index_0 = i * 3;
                    int index_1 = index_0 + 1;
                    int index_2 = index_0 + 2;

                    ai_mesh->mFaces[i] = aiFace();
                    auto& face = ai_mesh->mFaces[i];
                    face.mNumIndices = 3;
                    face.mIndices = new unsigned int[3];
                    face.mIndices[0] = raw_indices[index_2];
                    face.mIndices[1] = raw_indices[index_1];
                    face.mIndices[2] = raw_indices[index_0];
                }
            }

            model_uid_to_mesh_indices_map[raw_mesh["uid"]].push_back(mesh_index++);
        }
    }


    // load furniture
    Assimp::Importer importer;
    const auto& furniture = scene_json["furniture"];
    std::vector<aiMesh*> furniture_meshes;
    uint32_t total_mesh_count = pScene->mNumMeshes;
    std::vector<aiMaterial*> furniture_materials;
    uint32_t total_material_count = pScene->mNumMaterials;
    for (const auto& piece_of_furniture : furniture)
    {
        const auto& model_id = piece_of_furniture["jid"];
        for (const auto& furniture_directory : furniture_directories)
        {
            fs::path furniture_model_path = furniture_directory / fs::path(std::string(model_id));
            if (!fs::exists(furniture_model_path))
            {
                continue;
            }
            std::string obj_path_str = (furniture_model_path / fs::path("raw_model.obj")).string();
            const aiScene* furniture_model_scene = importer.ReadFile(
                obj_path_str.c_str(),
                aiProcess_Triangulate | aiProcess_OptimizeMeshes | aiProcess_SortByPType |
                aiProcess_ImproveCacheLocality | aiProcess_GenUVCoords // same flags as in assimp.cpp
            );
            assert(furniture_model_scene->mNumTextures == 0);

            // copy materials
            bool set_emissive = false;
            std::unordered_map<uint32_t, uint32_t> original_to_new_material_index_map;
            for (int i = 0; i < furniture_model_scene->mNumMaterials; i++)
            {
                auto* material_copy = new aiMaterial;
                aiMaterial::CopyPropertyList(material_copy, furniture_model_scene->mMaterials[i]);

                // edit texture path
                for (int prop_index = 0; prop_index < material_copy->mNumProperties; prop_index++)
                {
                    auto& property = material_copy->mProperties[prop_index];
                    if (property->mKey == aiString(_AI_MATKEY_TEXTURE_BASE))
                    {
                        // make texture path absolute so that vsg can find the file
                        std::string full_path = (furniture_model_path / fs::path(&property->mData[6])).string();
                        property->mDataLength = full_path.length() + 1;
                        char* new_data = new char[property->mDataLength + 4];
                        strcpy_s(&new_data[4], property->mDataLength, full_path.c_str());
                        delete property->mData;
                        property->mData = new_data;
                        property->mDataLength += 4;

                        // set prefix
                        property->mData[0] = static_cast<char>(full_path.length());
                        property->mData[1] = 0;
                        property->mData[2] = 0;
                        property->mData[3] = 0;
                    }
                }

                // add emission property if is lamp
                const std::string& obj_title = piece_of_furniture["title"];
                if (obj_title.find("lamp") != std::string::npos)
                {
                    aiColor3D emissive_color(_lamp_light_strength);
                    material_copy->AddProperty(&emissive_color, 1, AI_MATKEY_COLOR_EMISSIVE);
                    set_emissive = true;
                }


                furniture_materials.push_back(material_copy);
                original_to_new_material_index_map[i] = total_material_count++;
            }

            // copy meshes
            for (int i = 0; i < furniture_model_scene->mNumMeshes; i++)
            {
                aiMesh* mesh_copy;
                DeepCopyAiMesh(furniture_model_scene->mMeshes[i], mesh_copy);
                mesh_copy->mMaterialIndex = original_to_new_material_index_map[mesh_copy->mMaterialIndex];

                furniture_meshes.push_back(mesh_copy);
                model_uid_to_mesh_indices_map[piece_of_furniture["uid"]].push_back(total_mesh_count++);
            }
        }
    }
    // copy mesh data pointers to main scene
    auto* meshes_with_furniture = new aiMesh*[total_mesh_count];
    if (pScene->mMeshes)
    {
        std::memcpy(meshes_with_furniture, pScene->mMeshes, pScene->mNumMeshes * sizeof(aiMesh*));
    }
    std::copy(furniture_meshes.begin(), furniture_meshes.end(), &meshes_with_furniture[pScene->mNumMeshes]);
    delete[] pScene->mMeshes;
    pScene->mMeshes = meshes_with_furniture;
    pScene->mNumMeshes = total_mesh_count;
    // copy material data pointers to main scene
    if (total_material_count > 0)
    {
        auto* all_materials = new aiMaterial*[total_material_count];
        if (pScene->mMaterials && pScene->mNumMaterials > 0)
        {
            std::memcpy(all_materials, pScene->mMaterials, pScene->mNumMaterials * sizeof(aiMaterial*));
        }
        std::copy(furniture_materials.begin(), furniture_materials.end(), &all_materials[pScene->mNumMaterials]);
        delete[] pScene->mMaterials;
        pScene->mMaterials = all_materials;
        pScene->mNumMaterials = total_material_count;
    }

    // parse scene
    pScene->mRootNode = new aiNode;
    const auto& raw_rooms = scene_json["scene"]["room"];
    pScene->mRootNode->mNumChildren = raw_rooms.size();
    pScene->mRootNode->mChildren = new aiNode*[raw_rooms.size()];
    for (int room_index = 0; room_index < raw_rooms.size(); room_index++)
    {
        const auto& raw_room = raw_rooms[room_index];
        const auto& raw_children = raw_room["children"];

        pScene->mRootNode->mChildren[room_index] = new aiNode;
        auto& room_node = pScene->mRootNode->mChildren[room_index];
        room_node->mName = raw_room["instanceid"];
        room_node->mParent = pScene->mRootNode;
        room_node->mNumChildren = raw_children.size();
        room_node->mChildren = new aiNode*[raw_children.size()];

        for (int child_index = 0; child_index < raw_children.size(); child_index++)
        {
            const auto& raw_child = raw_children[child_index];
            room_node->mChildren[child_index] = new aiNode;

            std::string instance_id = raw_child["instanceid"];
            auto& child_node = room_node->mChildren[child_index];
            child_node->mName = instance_id;

            if (instance_id.find("furniture") != std::string::npos)
            {
                // create transformation
                const auto& scale = raw_child["scale"];
                const auto& rotation = raw_child["rot"];
                const auto& position = raw_child["pos"];
                child_node->mTransformation = aiMatrix4x4(
                    aiVector3D(scale[0], scale[1], scale[2]),
                    aiQuaternion(rotation[3], rotation[0], rotation[2], rotation[1]),
                    aiVector3D(position[0], position[2], position[1])
                );
                aiMatrix4x4 scale_matrix;
                aiMatrix4x4::Scaling(aiVector3D(1, -1, 1), scale_matrix);
                child_node->mTransformation = scale_matrix * child_node->mTransformation;
            }
            else
            {
                // added to prevent floors from disappering without changing their position
                aiMatrix4x4::Translation(aiVector3D(0, 0, -5), child_node->mTransformation);
            }
            

            child_node->mParent = room_node;

            auto& mesh_indices = model_uid_to_mesh_indices_map[raw_child["ref"]];
            child_node->mNumMeshes = mesh_indices.size();
            child_node->mMeshes = new unsigned int[child_node->mNumMeshes];
            std::copy(mesh_indices.begin(), mesh_indices.end(), child_node->mMeshes);
        }
    }
}
void AI3DFrontImporter::FindDataDirectories(const std::string& file_path,
                                            std::vector<fs::path>& texture_directories, std::vector<fs::path>& furniture_directories)
{
    auto root_path_3d_front = fs::absolute(fs::path(file_path)).parent_path().parent_path();
    for (const auto& directory_entry : fs::directory_iterator(root_path_3d_front))
    {
        if (!directory_entry.is_directory())
        {
            continue;
        }
        const fs::path& directory_path = directory_entry.path();
        std::string directory_name = directory_path.filename().string();
        if (directory_name.find("3D-FUTURE-model") != std::string::npos)
        {
            furniture_directories.push_back(directory_path);
        }
        else if (directory_name.find("3D-FRONT-texture") != std::string::npos)
        {
            texture_directories.push_back(directory_path);
        }
    }
}
