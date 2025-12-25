#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

#include "PmxModel.hpp"
#include "PmxLoader.hpp"
#include "BinaryReader.hpp"

using namespace std;

static void WriteUtf8Bom(std::ofstream& os)
{
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    os.write(reinterpret_cast<const char*>(bom), sizeof(bom));
}

static std::string WToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static std::string PathToUtf8(const std::filesystem::path& p)
{
    return WToUtf8(p.wstring());
}

static std::ofstream OpenOutBinaryOrThrow(const std::filesystem::path& outPath)
{
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    std::ofstream os(outPath, std::ios::binary);
    if (!os)
    {
        throw std::runtime_error("Failed to open output file: " + PathToUtf8(outPath));
    }
    return os;
}

static void SetupConsoleUtf8()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

struct Logger
{
    std::ofstream log;

    void Open(const std::filesystem::path& path)
    {
        log.open(path, std::ios::binary);
        if (log) WriteUtf8Bom(log);
    }

    void PrintLn(const std::string& s)
    {
        std::cout << s << "\n";
        std::cout.flush();
        if (log)
        {
            log << s << "\n";
            log.flush();
        }
    }

    void PrintErrLn(const std::string& s)
    {
        std::cerr << s << "\n";
        std::cerr.flush();
        if (log)
        {
            log << s << "\n";
            log.flush();
        }
    }
};

static std::wstring Utf8ToW(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

static std::string EscapeJson(std::string_view s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
            case '\"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20)
                {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)(unsigned char)c);
                    o += buf;
                }
                else
                {
                    o += c;
                }
                break;
        }
    }
    return o;
}

static std::string TsvField(const std::string& s)
{
    std::string o = s;
    for (char& c : o)
    {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    return o;
}

static const char* RigidShapeName(PmxModel::RigidBody::ShapeType t)
{
    using ST = PmxModel::RigidBody::ShapeType;
    switch (t)
    {
        case ST::Sphere:  return "Sphere";
        case ST::Box:     return "Box";
        case ST::Capsule: return "Capsule";
        default:          return "Unknown";
    }
}

static const char* RigidOpName(PmxModel::RigidBody::OperationType t)
{
    using OT = PmxModel::RigidBody::OperationType;
    switch (t)
    {
        case OT::Static:                  return "Static(0)";
        case OT::Dynamic:                 return "Dynamic(1)";
        case OT::DynamicAndPositionAdjust:return "DynamicAndPositionAdjust(2)";
        default:                          return "Unknown";
    }
}

static bool LooksLikeDegrees3(const DirectX::XMFLOAT3& v)
{
    auto ax = std::abs(v.x);
    auto ay = std::abs(v.y);
    auto az = std::abs(v.z);
    return (ax > 20.0f) || (ay > 20.0f) || (az > 20.0f);
}

static bool ContainsFilter(const std::wstring& s, const std::wstring& filter)
{
    if (filter.empty()) return true;
    return s.find(filter) != std::wstring::npos;
}

static void WriteJsonFloat3(std::ostream& os, const DirectX::XMFLOAT3& v)
{
    os << "[" << v.x << "," << v.y << "," << v.z << "]";
}

static void DumpSummaryJson(const PmxModel& model, const std::filesystem::path& outPath,
                            const std::wstring& filter)
{
    std::ofstream os = OpenOutBinaryOrThrow(outPath);
    WriteUtf8Bom(os);

    float minx, miny, minz, maxx, maxy, maxz;
    model.GetBounds(minx, miny, minz, maxx, maxy, maxz);

    const auto& h = model.GetHeader();
    os << "{\n";
    os << "  \"path\": \"" << EscapeJson(WToUtf8(model.Path().wstring())) << "\",\n";
    os << "  \"revision\": " << model.Revision() << ",\n";
    os << "  \"header\": {\n";
    os << "    \"version\": " << h.version << ",\n";
    os << "    \"encoding\": " << (int)h.encoding << ",\n";
    os << "    \"additionalUV\": " << (int)h.additionalUV << ",\n";
    os << "    \"vertexIndexSize\": " << (int)h.vertexIndexSize << ",\n";
    os << "    \"textureIndexSize\": " << (int)h.textureIndexSize << ",\n";
    os << "    \"materialIndexSize\": " << (int)h.materialIndexSize << ",\n";
    os << "    \"boneIndexSize\": " << (int)h.boneIndexSize << ",\n";
    os << "    \"morphIndexSize\": " << (int)h.morphIndexSize << ",\n";
    os << "    \"rigidIndexSize\": " << (int)h.rigidIndexSize << "\n";
    os << "  },\n";
    os << "  \"bounds\": {\"min\": [" << minx << "," << miny << "," << minz << "], "
        << "\"max\": [" << maxx << "," << maxy << "," << maxz << "]},\n";

    os << "  \"counts\": {\n";
    os << "    \"vertices\": " << model.Vertices().size() << ",\n";
    os << "    \"indices\": " << model.Indices().size() << ",\n";
    os << "    \"materials\": " << model.Materials().size() << ",\n";
    os << "    \"bones\": " << model.Bones().size() << ",\n";
    os << "    \"rigidBodies\": " << model.RigidBodies().size() << ",\n";
    os << "    \"joints\": " << model.Joints().size() << "\n";
    os << "  },\n";

    const auto& bones = model.Bones();
    os << "  \"bones\": [\n";
    bool firstBone = true;
    for (size_t i = 0; i < bones.size(); ++i)
    {
        const auto& b = bones[i];
        if (!filter.empty() && !ContainsFilter(b.name, filter) && !ContainsFilter(b.nameEn, filter))
            continue;

        if (!firstBone) os << ",\n";
        firstBone = false;

        os << "    {\n";
        os << "      \"index\": " << i << ",\n";
        os << "      \"name\": \"" << EscapeJson(WToUtf8(b.name)) << "\",\n";
        os << "      \"nameEn\": \"" << EscapeJson(WToUtf8(b.nameEn)) << "\",\n";
        os << "      \"position\": "; WriteJsonFloat3(os, b.position); os << ",\n";
        os << "      \"parentIndex\": " << b.parentIndex << ",\n";
        os << "      \"layer\": " << b.layer << ",\n";
        os << "      \"flags\": " << b.flags << ",\n";
        os << "      \"isIK\": " << (b.IsIK() ? "true" : "false") << ",\n";
        os << "      \"afterPhysics\": " << (b.IsAfterPhysics() ? "true" : "false") << ",\n";
        os << "      \"ikTargetIndex\": " << b.ikTargetIndex << ",\n";
        os << "      \"ikLoopCount\": " << b.ikLoopCount << ",\n";
        os << "      \"ikLimitAngle\": " << b.ikLimitAngle << ",\n";
        os << "      \"grantParentIndex\": " << b.grantParentIndex << ",\n";
        os << "      \"grantWeight\": " << b.grantWeight << ",\n";
        os << "      \"ikLinks\": [";
        for (size_t k = 0; k < b.ikLinks.size(); ++k)
        {
            const auto& lk = b.ikLinks[k];
            if (k) os << ",";
            os << "{";
            os << "\"boneIndex\":" << lk.boneIndex << ",";
            os << "\"hasLimit\":" << (lk.hasLimit ? "true" : "false") << ",";
            os << "\"limitMin\":"; WriteJsonFloat3(os, lk.limitMin); os << ",";
            os << "\"limitMax\":"; WriteJsonFloat3(os, lk.limitMax);
            os << "}";
        }
        os << "]\n";
        os << "    }";
    }
    os << "\n  ],\n";

    os << "  \"rigidBodies\": [\n";
    const auto& rigs = model.RigidBodies();
    bool firstRig = true;
    for (size_t i = 0; i < rigs.size(); ++i)
    {
        const auto& r = rigs[i];

        std::wstring boneName, boneNameEn;
        if (r.boneIndex >= 0 && (size_t)r.boneIndex < bones.size())
        {
            boneName = bones[(size_t)r.boneIndex].name;
            boneNameEn = bones[(size_t)r.boneIndex].nameEn;
        }

        if (!filter.empty())
        {
            bool ok = ContainsFilter(r.name, filter) || ContainsFilter(r.nameEn, filter) ||
                ContainsFilter(boneName, filter) || ContainsFilter(boneNameEn, filter);
            if (!ok) continue;
        }

        if (!firstRig) os << ",\n";
        firstRig = false;

        os << "    {\n";
        os << "      \"index\": " << i << ",\n";
        os << "      \"name\": \"" << EscapeJson(WToUtf8(r.name)) << "\",\n";
        os << "      \"nameEn\": \"" << EscapeJson(WToUtf8(r.nameEn)) << "\",\n";
        os << "      \"boneIndex\": " << r.boneIndex << ",\n";
        os << "      \"boneName\": \"" << EscapeJson(WToUtf8(boneName)) << "\",\n";
        os << "      \"groupIndex\": " << (int)r.groupIndex << ",\n";
        os << "      \"ignoreCollisionGroup\": " << r.ignoreCollisionGroup << ",\n";
        os << "      \"shapeType\": \"" << RigidShapeName(r.shapeType) << "\",\n";
        os << "      \"shapeSize\": "; WriteJsonFloat3(os, r.shapeSize); os << ",\n";
        os << "      \"position\": "; WriteJsonFloat3(os, r.position); os << ",\n";
        os << "      \"rotation\": "; WriteJsonFloat3(os, r.rotation); os << ",\n";
        os << "      \"mass\": " << r.mass << ",\n";
        os << "      \"linearDamping\": " << r.linearDamping << ",\n";
        os << "      \"angularDamping\": " << r.angularDamping << ",\n";
        os << "      \"restitution\": " << r.restitution << ",\n";
        os << "      \"friction\": " << r.friction << ",\n";
        os << "      \"operation\": \"" << RigidOpName(r.operation) << "\"\n";
        os << "    }";
    }
    os << "\n  ],\n";

    os << "  \"joints\": [\n";
    const auto& joints = model.Joints();
    bool firstJoint = true;
    for (size_t i = 0; i < joints.size(); ++i)
    {
        const auto& j = joints[i];

        std::wstring aName, bName;
        if (j.rigidBodyA >= 0 && (size_t)j.rigidBodyA < rigs.size()) aName = rigs[(size_t)j.rigidBodyA].name;
        if (j.rigidBodyB >= 0 && (size_t)j.rigidBodyB < rigs.size()) bName = rigs[(size_t)j.rigidBodyB].name;

        if (!filter.empty())
        {
            bool ok = ContainsFilter(j.name, filter) || ContainsFilter(j.nameEn, filter) ||
                ContainsFilter(aName, filter) || ContainsFilter(bName, filter);
            if (!ok) continue;
        }

        if (!firstJoint) os << ",\n";
        firstJoint = false;

        os << "    {\n";
        os << "      \"index\": " << i << ",\n";
        os << "      \"name\": \"" << EscapeJson(WToUtf8(j.name)) << "\",\n";
        os << "      \"nameEn\": \"" << EscapeJson(WToUtf8(j.nameEn)) << "\",\n";
        os << "      \"rigidBodyA\": " << j.rigidBodyA << ",\n";
        os << "      \"rigidBodyB\": " << j.rigidBodyB << ",\n";
        os << "      \"rigidBodyAName\": \"" << EscapeJson(WToUtf8(aName)) << "\",\n";
        os << "      \"rigidBodyBName\": \"" << EscapeJson(WToUtf8(bName)) << "\",\n";
        os << "      \"position\": "; WriteJsonFloat3(os, j.position); os << ",\n";
        os << "      \"rotation\": "; WriteJsonFloat3(os, j.rotation); os << ",\n";
        os << "      \"positionLower\": "; WriteJsonFloat3(os, j.positionLower); os << ",\n";
        os << "      \"positionUpper\": "; WriteJsonFloat3(os, j.positionUpper); os << ",\n";
        os << "      \"rotationLower\": "; WriteJsonFloat3(os, j.rotationLower); os << ",\n";
        os << "      \"rotationUpper\": "; WriteJsonFloat3(os, j.rotationUpper); os << ",\n";
        os << "      \"springPosition\": "; WriteJsonFloat3(os, j.springPosition); os << ",\n";
        os << "      \"springRotation\": "; WriteJsonFloat3(os, j.springRotation); os << ",\n";
        os << "      \"warnDegreesLike\": " << ((LooksLikeDegrees3(j.rotationLower) || LooksLikeDegrees3(j.rotationUpper)) ? "true" : "false") << "\n";
        os << "    }";
    }
    os << "\n  ]\n";
    os << "}\n";
}


static void DumpBonesTsv(const PmxModel& model, const std::filesystem::path& outPath)
{
    std::ofstream os = OpenOutBinaryOrThrow(outPath);
    WriteUtf8Bom(os);
    os << "index\tname\tnameEn\tposX\tposY\tposZ\tparent\tlayer\tflags\tafterPhysics\tisIK\tikTarget\tikLoop\tikLimit\tgrantParent\tgrantWeight\n";

    const auto& bones = model.Bones();
    for (size_t i = 0; i < bones.size(); ++i)
    {
        const auto& b = bones[i];
        os << i << "\t"
            << TsvField(WToUtf8(b.name)) << "\t"
            << TsvField(WToUtf8(b.nameEn)) << "\t"
            << b.position.x << "\t" << b.position.y << "\t" << b.position.z << "\t"
            << b.parentIndex << "\t"
            << b.layer << "\t"
            << b.flags << "\t"
            << (b.IsAfterPhysics() ? 1 : 0) << "\t"
            << (b.IsIK() ? 1 : 0) << "\t"
            << b.ikTargetIndex << "\t"
            << b.ikLoopCount << "\t"
            << b.ikLimitAngle << "\t"
            << b.grantParentIndex << "\t"
            << b.grantWeight
            << "\n";
    }
}

static void DumpRigidBodiesTsv(const PmxModel& model, const std::filesystem::path& outPath)
{
    std::ofstream os = OpenOutBinaryOrThrow(outPath);
    WriteUtf8Bom(os);
    os << "index\tname\tnameEn\tboneIndex\tboneName\tgroup\tignoreMask\tshapeType\tsizeX\tsizeY\tsizeZ\tposX\tposY\tposZ\trotX\trotY\trotZ\tmass\tlinDamp\tangDamp\trest\tfric\top\n";

    const auto& bones = model.Bones();
    const auto& rigs = model.RigidBodies();
    for (size_t i = 0; i < rigs.size(); ++i)
    {
        const auto& r = rigs[i];
        std::wstring boneName;
        if (r.boneIndex >= 0 && (size_t)r.boneIndex < bones.size())
            boneName = bones[(size_t)r.boneIndex].name;

        os << i << "\t"
            << TsvField(WToUtf8(r.name)) << "\t"
            << TsvField(WToUtf8(r.nameEn)) << "\t"
            << r.boneIndex << "\t"
            << TsvField(WToUtf8(boneName)) << "\t"
            << (int)r.groupIndex << "\t"
            << r.ignoreCollisionGroup << "\t"
            << RigidShapeName(r.shapeType) << "\t"
            << r.shapeSize.x << "\t" << r.shapeSize.y << "\t" << r.shapeSize.z << "\t"
            << r.position.x << "\t" << r.position.y << "\t" << r.position.z << "\t"
            << r.rotation.x << "\t" << r.rotation.y << "\t" << r.rotation.z << "\t"
            << r.mass << "\t"
            << r.linearDamping << "\t"
            << r.angularDamping << "\t"
            << r.restitution << "\t"
            << r.friction << "\t"
            << RigidOpName(r.operation) << "\n";
    }
}

static void DumpJointsTsv(const PmxModel& model, const std::filesystem::path& outPath)
{
    std::ofstream os = OpenOutBinaryOrThrow(outPath);
    WriteUtf8Bom(os);
    os << "index\tname\tnameEn\trigidA\trigidAName\trigidB\trigidBName\tposX\tposY\tposZ\trotX\trotY\trotZ\tposLX\tposLY\tposLZ\tposUX\tposUY\tposUZ\trotLX\trotLY\trotLZ\trotUX\trotUY\trotUZ\tsprPosX\tsprPosY\tsprPosZ\tsprRotX\tsprRotY\tsprRotZ\twarnDegreesLike\n";

    const auto& rigs = model.RigidBodies();
    const auto& joints = model.Joints();
    for (size_t i = 0; i < joints.size(); ++i)
    {
        const auto& j = joints[i];
        std::wstring aName, bName;
        if (j.rigidBodyA >= 0 && (size_t)j.rigidBodyA < rigs.size()) aName = rigs[(size_t)j.rigidBodyA].name;
        if (j.rigidBodyB >= 0 && (size_t)j.rigidBodyB < rigs.size()) bName = rigs[(size_t)j.rigidBodyB].name;

        bool warnDeg = LooksLikeDegrees3(j.rotationLower) || LooksLikeDegrees3(j.rotationUpper);

        os << i << "\t"
            << TsvField(WToUtf8(j.name)) << "\t"
            << TsvField(WToUtf8(j.nameEn)) << "\t"
            << j.rigidBodyA << "\t"
            << TsvField(WToUtf8(aName)) << "\t"
            << j.rigidBodyB << "\t"
            << TsvField(WToUtf8(bName)) << "\t"
            << j.position.x << "\t" << j.position.y << "\t" << j.position.z << "\t"
            << j.rotation.x << "\t" << j.rotation.y << "\t" << j.rotation.z << "\t"
            << j.positionLower.x << "\t" << j.positionLower.y << "\t" << j.positionLower.z << "\t"
            << j.positionUpper.x << "\t" << j.positionUpper.y << "\t" << j.positionUpper.z << "\t"
            << j.rotationLower.x << "\t" << j.rotationLower.y << "\t" << j.rotationLower.z << "\t"
            << j.rotationUpper.x << "\t" << j.rotationUpper.y << "\t" << j.rotationUpper.z << "\t"
            << j.springPosition.x << "\t" << j.springPosition.y << "\t" << j.springPosition.z << "\t"
            << j.springRotation.x << "\t" << j.springRotation.y << "\t" << j.springRotation.z << "\t"
            << (warnDeg ? 1 : 0)
            << "\n";
    }
}

static void DumpReportTxt(const PmxModel& model, const std::filesystem::path& outPath)
{
    std::ofstream os = OpenOutBinaryOrThrow(outPath);
    WriteUtf8Bom(os);

    const auto& bones = model.Bones();
    const auto& rigs = model.RigidBodies();
    const auto& joints = model.Joints();

    int op0 = 0, op1 = 0, op2 = 0;
    for (auto& r : rigs)
    {
        using OT = PmxModel::RigidBody::OperationType;
        if (r.operation == OT::Static) op0++;
        else if (r.operation == OT::Dynamic) op1++;
        else if (r.operation == OT::DynamicAndPositionAdjust) op2++;
    }

    os << "PMX: " << WToUtf8(model.Path().wstring()) << "\n";
    os << "Revision: " << model.Revision() << "\n";
    os << "Counts: bones=" << bones.size() << ", rigidBodies=" << rigs.size() << ", joints=" << joints.size() << "\n";
    os << "RigidBody operation counts: Static=" << op0 << ", Dynamic=" << op1 << ", DynamicAndPositionAdjust=" << op2 << "\n\n";

    os << "[Warnings]\n";

    int degWarnCount = 0;
    for (size_t i = 0; i < joints.size(); ++i)
    {
        const auto& j = joints[i];
        if (LooksLikeDegrees3(j.rotationLower) || LooksLikeDegrees3(j.rotationUpper))
        {
            degWarnCount++;
            os << "  Joint[" << i << "] rotation limits look like degrees: "
                << TsvField(WToUtf8(j.name)) << "\n";
            os << "    rotLower=(" << j.rotationLower.x << "," << j.rotationLower.y << "," << j.rotationLower.z << ")\n";
            os << "    rotUpper=(" << j.rotationUpper.x << "," << j.rotationUpper.y << "," << j.rotationUpper.z << ")\n";
        }
    }
    if (!degWarnCount) os << "  (no degree-like joint rotation limits detected)\n";

    os << "\n[Suspicious rigid bodies]\n";
    for (size_t i = 0; i < rigs.size(); ++i)
    {
        const auto& r = rigs[i];
        bool bad = false;
        if (!(r.mass >= 0.0f) || !std::isfinite(r.mass)) bad = true;
        if (!std::isfinite(r.linearDamping) || r.linearDamping < 0.0f || r.linearDamping > 1.0f) bad = true;
        if (!std::isfinite(r.angularDamping) || r.angularDamping < 0.0f || r.angularDamping > 1.0f) bad = true;
        if (LooksLikeDegrees3(r.rotation)) bad = true;

        if (bad)
        {
            std::wstring boneName;
            if (r.boneIndex >= 0 && (size_t)r.boneIndex < bones.size()) boneName = bones[(size_t)r.boneIndex].name;

            os << "  Rigid[" << i << "] " << WToUtf8(r.name) << " (bone=" << WToUtf8(boneName) << ")\n";
            os << "    op=" << RigidOpName(r.operation)
                << " mass=" << r.mass
                << " linD=" << r.linearDamping
                << " angD=" << r.angularDamping
                << " rot=(" << r.rotation.x << "," << r.rotation.y << "," << r.rotation.z << ")\n";
        }
    }
}

static void PrintUsage()
{
    std::wcout << L"Usage:\n";
    std::wcout << L"  PmxInspect.exe <model.pmx> [--out <dir>] [--filter <substring>]\n";
}

int wmain(int argc, wchar_t** argv)
{
    SetupConsoleUtf8();
    Logger logger;
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    std::filesystem::path pmxPath = argv[1];
    std::filesystem::path outDir;
    std::wstring filter;

    for (int i = 2; i < argc; ++i)
    {
        std::wstring a = argv[i];
        if (a == L"--out" && i + 1 < argc)
        {
            outDir = argv[++i];
        }
        else if (a == L"--filter" && i + 1 < argc)
        {
            filter = argv[++i];
        }
    }

    if (outDir.empty())
    {
        auto base = pmxPath.stem().wstring();
        outDir = std::filesystem::current_path() / (base + L"_pmx_dump");
    }

    try
    {
        std::filesystem::create_directories(outDir);
    }
    catch (const std::exception& e)
    {
        logger.PrintErrLn(std::string("create_directories failed: ") + PathToUtf8(outDir) + " : " + e.what());
        return 3;
    }

    try
    {
        logger.Open(outDir / L"run.log");
    }
    catch (...)
    {

    }

    logger.PrintLn(std::string("pmxPath: ") + PathToUtf8(pmxPath));
    logger.PrintLn(std::string("outDir : ") + PathToUtf8(outDir));

    std::error_code ec;
    bool pmxExists = std::filesystem::exists(pmxPath, ec);
    logger.PrintLn(std::string("pmxExists: ") + (pmxExists ? "true" : "false"));
    if (pmxExists)
    {
        auto sz = std::filesystem::file_size(pmxPath, ec);
        if (!ec) logger.PrintLn(std::string("pmxSize  : ") + std::to_string((unsigned long long)sz) + " bytes");
    }


    PmxModel model;
    bool ok = false;
    try
    {
        ok = model.Load(pmxPath, [&](float t, const wchar_t* msg) {
            std::ostringstream ss;
            ss << "[PMX] ";
            ss << (int)(t * 100.0f) << "% ";
            if (msg) ss << WToUtf8(std::wstring(msg));
            logger.PrintLn(ss.str());
                        });
    }
    catch (const std::exception& e)
    {
        logger.PrintErrLn(std::string("Exception while loading PMX: ") + e.what());
        return 2;
    }

    if (!ok)
    {
        logger.PrintErrLn(std::string("Load returned false: ") + PathToUtf8(pmxPath));
        return 2;
    }

    {
        std::ostringstream ss;
        ss << "Loaded OK. vertices=" << model.Vertices().size()
            << " indices=" << model.Indices().size()
            << " bones=" << model.Bones().size()
            << " rigidBodies=" << model.RigidBodies().size()
            << " joints=" << model.Joints().size();
        logger.PrintLn(ss.str());
    }

    try
    {
        DumpSummaryJson(model, outDir / L"summary.json", filter);
        DumpBonesTsv(model, outDir / L"bones.tsv");
        DumpRigidBodiesTsv(model, outDir / L"rigid_bodies.tsv");
        DumpJointsTsv(model, outDir / L"joints.tsv");
        DumpReportTxt(model, outDir / L"report.txt");
    }
    catch (const std::exception& e)
    {
        logger.PrintErrLn(std::string("Exception while writing outputs: ") + e.what());
        return 5;
    }

    logger.PrintLn(std::string("Dumped to: ") + PathToUtf8(outDir));
    return 0;
}