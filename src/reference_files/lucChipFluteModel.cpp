#include "lucChipFluteModel.h"

#include <lucCollisionDetectionFilter.h>
#include <lucCylinderSource.h>
#include <lucDoubleArray.h>
#include <lucMath.h>
#include <lucMatrix4x4.h>
#include <lucSegArray.h>
#include <lucToolPathDataArray.h>
#include <lucTransform.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeCircle.hxx>
#include <GC_MakeSegment.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Curve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_bsplineCurve.hxx>
#include <TColgp_HSequenceOfPnt.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <array>
#include <chrono>
#include <future>
#include <gp_Circ.hxx>
#include <mutex>
#include <semaphore>
#include <thread>

#include "GrindingWheelSweptVolume.h"
#include "GrindingWheelSweptVolumeLine.h"
#include "UnNamed.h"
#include "grindingwheelsweptvolumeboolean.h"
#include "lucBarrelEndMillFlute.h"
#include "lucClearanceModel.h"
#include "lucCutterCylineFlute.h"
#include "lucCutterFluteGeometricParameter.h"
#include "lucCutterGeometricParameter.h"
#include "lucCutterModelAlgorithm.h"
#include "lucCutterProcess.h"
#include "lucCutterProcessStepBase.h"
#include "lucFeedrate.h"
#include "lucGrindingWheel.h"
#include "lucMillingFlatCutterGeometricParameter.h"
#include "lucObjectFactory.h"
#include "lucOthersBarrelCutterGeometricParameter.h"
#include "lucSmartPointer.h"
#include "lucToolPath.h"
#include "lucWoodToolFlute.h"

LUC_ABI_NAMESPACE_BEGIN
lucStandardNewMacro(lucChipFluteModel);

using range_ = std::pair<Standard_Real, Standard_Real>;
using allPSet_ = std::vector<std::vector<Handle(TColgp_HSequenceOfPnt)>>;
using pSets_ = std::vector<Handle(TColgp_HSequenceOfPnt)>;
using coord_ = std::tuple<Standard_Real, Standard_Real, Standard_Real>;

void write_file(std::vector<Handle(TColgp_HSequenceOfPnt)> pSets)
{
    std::ofstream ofs(
        R"(D:\Data\Study\vulkan\FirstApp\res\toolpath\flute_for_model_nx.dat)",
        std::ios::out | std::ios::trunc);

    if (!ofs.is_open()) {
        std::cerr << "Failed to open flute.toolpath\n";
        return;
    }

    ofs << std::fixed << std::setprecision(6);

    for (int segId = 0; segId < pSets.size(); ++segId) {
        ofs << "new seg\n";

        for (lucIdType k = 1; k <= pSets[segId]->Size(); ++k) {
            gp_Pnt p = pSets[segId]->Value(k);
            ofs << p.X() << " " << p.Y() << " " << p.Z() << " "
                << "\n";
        }
    }

    ofs.close();
}
lucChipFluteModel::lucChipFluteModel()
{
    std::cout << "Construct lucChipFluteModel" << std::endl;
}

void lucChipFluteModel::InitDefaultGrindingWheel()
{
    //设置砂轮参数
    // 1V1
    if (m_gWheel == nullptr) m_gWheel = lucGrindingWheel::New();
    m_gWheel->SetType(GrindingWheel::PERIPHERY);
    m_gWheel->SetDiameter(98.31);
    m_gWheel->SetAngle(55.39);
    m_gWheel->SetWidth(15.);
    m_gWheel->SetFillet(0.1);
    m_gWheel->SetFillet_Littleende(0.1);
}

void lucChipFluteModel::InitGrindingWheelEquation()
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;

    Standard_Real gR = m_gWheel->GetDiameter() / 2;
    Standard_Real gr1 = m_gWheel->GetFillet();
    Standard_Real gr2 = m_gWheel->GetFillet_Littleende();
    Standard_Real gb = m_gWheel->GetWidth();
    Standard_Real ga = M_PI_2 - lucMath::RadiansFromDegrees(
                                    m_gWheel->GetAngle());  //算法中ga与砂轮角度互余

    switch (m_gWheel->GetType()) {
        case GrindingWheel::PERIPHERY: {
            /*平砂轮*/
            std::cout << "---=PERIPHERY=--" << std::endl;
            if (ga == M_PI_2) {
                /*1A1*/
                std::cout << "---=1A1=--" << std::endl;
                m_gRanges = {range_(0, gr1), range_(gr1, gb - gr1), range_(gb - gr1, gb)};
                auto fg12_expr = [=](const Standard_Real& m1) {
                    return gR - gr1 + sqrt(gr1 * gr1 - (gr1 - m1) * (gr1 - m1));
                };
                auto fg23_expr = [=](const Standard_Real& m1) { return gR; };
                auto fg34_expr = [=](const Standard_Real& m1) {
                    return gR - gr2 +
                           sqrt(gr2 * gr2 - (m1 - (gb - gr2)) * (m1 - (gb - gr2)));
                };
                if (m_isOptimized) {
                    m_gEquations[0] = fg12_expr;
                    m_gEquations[1] = fg23_expr;
                    m_gEquations[2] = fg34_expr;
                } else {
                    m_gEquations.push_back(fg12_expr);
                    m_gEquations.push_back(fg23_expr);
                    m_gEquations.push_back(fg34_expr);
                }
            } else if (ga > 0 && M_PI_2 - ga > 1e-2) {
                /*1V1*/
                std::cout << "---=1V1=--" << std::endl;
                m_gRanges = {range_(0, gr1 + gr1 * cos(ga)),
                             range_(gr1 + gr1 * cos(ga), gb - (gr2 - gr2 * cos(ga))),
                             range_(gb - (gr2 - gr2 * cos(ga)), gb)};
                auto fg12_expr = [=](const double& m1) {
                    return gR - gr1 + sqrt(gr1 * gr1 - (gr1 - m1) * (gr1 - m1));
                };
                auto fg23_expr = [=](const double& m1) {
                    return gR - gr1 + gr1 * sin(ga) -
                           (m1 - (gr1 + gr1 * cos(ga))) * (1 / tan(ga));
                };
                auto fg34_expr = [=](const double& m1) {
                    return gR - gr1 * (1 - 1 / tan(ga / 2)) - gb / tan(ga) -
                           gr2 * tan(ga / 2) +
                           sqrt(gr2 * gr2 - (gr2 - gb + m1) * (gr2 - gb + m1));
                };
                if (m_isOptimized) {
                    m_gEquations[0] = fg12_expr;
                    m_gEquations[1] = fg23_expr;
                    m_gEquations[2] = fg34_expr;
                } else {
                    m_gEquations.push_back(fg12_expr);
                    m_gEquations.push_back(fg23_expr);
                    m_gEquations.push_back(fg34_expr);
                }
            }
            break;
        }
        default:
            break;
    }

    return;
}

// chipFlutes suppose to be a private member and deleted in destruct function
Standard_Integer lucChipFluteModel::CreateChipFlute(
    std::vector<ChipFlute*>& chipFlutes,
    lucSmartPointer<lucCutterGeometricParameter> geomParam,
    lucSmartPointer<lucCutterProcess> process)
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;

    //获取加工步骤参数
    std::vector<lucCutterProcessStepBase*> stepBaseVec = process->GetProcessSteps();

    m_geomParam = lucCutterGeometricParameter::New();
    m_geomParam->DeepCopy(geomParam);
    m_processParam = lucCutterProcess::New();
    m_processParam->DeepCopy(process);
    InitDefaultGrindingWheel();

    m_rBlade = m_geomParam->GetOutsideDiameter() / 2;
    m_lead = M_PI * m_geomParam->GetOutsideDiameter() /
             tan(lucMath::RadiansFromDegrees(m_geomParam->GetHelixAngle()));

    //砂轮位姿
    std::vector<double> O5_;
    std::vector<double> ori_;
    std::vector<lucMatrix4x4*> matrixes = {};

    m_fluteParam = lucSmartPointer<lucCutterFluteGeometricParameter>::New();
    m_proStepBase = lucSmartPointer<lucCutterProcessStepBase>::New();
    for (Standard_Integer index = 0; index < stepBaseVec.size(); index++) {
        int opType = stepBaseVec[index]->GetOperationType();
        if (opType == lucProcessStepType::Operation::Flute ||
            opType == lucProcessStepType::Operation::WoodToolFlute ||
            opType == lucProcessStepType::Operation::BarrelFlute ||
            opType == lucProcessStepType::Operation::FormFlute) {
            m_proStepBase->DeepCopy(stepBaseVec[index]);
            m_fluteParam = static_cast<lucCutterFluteGeometricParameter*>(
                m_proStepBase->GetProcessGeometricParameter());
            m_gWheel = m_proStepBase->GetGrindingWheel();

            if (m_gWheel == nullptr) {
                std::cout << "---m_gWheel is null, use default grinding wheel"
                          << std::endl;
                InitDefaultGrindingWheel();
            } else {
                std::cout << "---m_gWheel is not null" << std::endl;
                /*暂时仅支持1A1和1V1砂轮*/
                if (m_gWheel->GetType() != GrindingWheel::PERIPHERY)
                    InitDefaultGrindingWheel();
            }

            if (opType == lucProcessStepType::Operation::WoodToolFlute) {
                std::cout << "--- wood tool flute ---" << std::endl;
                m_woodParam =
                    lucMillingFlatCutterGeometricParameter::SafeDownCast(geomParam);
                if (m_woodParam == nullptr) {
                    std::cerr << "---cast wood flute param failed" << std::endl;
                    return -1;
                }
            } else if (opType == lucProcessStepType::Operation::BarrelFlute) {
                std::cout << "--- barrel flute ---" << std::endl;
                m_barrelParam =
                    lucOthersBarrelCutterGeometricParameter::SafeDownCast(geomParam);
                if (m_barrelParam == nullptr) {
                    std::cerr << "---cast barrel flute param failed" << std::endl;
                    return -1;
                }
            }

            m_neededOptimized = m_fluteParam->GetIsOptimizeWheel();
            if (opType == lucProcessStepType::Operation::BarrelFlute &&
                m_fluteParam->GetOptimizationStrategyType() ==
                    lucCutterFluteGeometricParameter::Arc) {
                /*鼓形刀且选择优化砂轮位姿，自动添加优化砂轮参数*/
                std::cout << "-----------------------------------------------------------"
                             "-----------------------------"
                          << std::endl;
                m_neededOptimized = Standard_True;
                m_fluteParam->SetIsOptimizeWheel(Standard_True);
            }
        }
    }

    InitGrindingWheelEquation();
    //获取砂轮位姿
    matrixes = CalculateGrindWheelPose();

    //获得的砂轮位姿数量与齿数不等
    if (matrixes.size() != geomParam->GetTeethNumber()) {
        std::cerr << "Error matrix number = " << matrixes.size() << std::endl;
        return -1;
    }

    std::cout << "get matrixes size = " << matrixes.size() << std::endl;

    if (m_geomParam->GetHelixType() ==
        lucCutterGeometricParameter::HelixType::Straight_Others)
        m_isStraight = Standard_False;  //暂时屏蔽直槽

    //计算容屑槽点集
    pSets_ pSets;
    int count = 0;
    for (const auto& iter : matrixes) {
        PrintMatrix("m", iter);
        Handle(TColgp_HSequenceOfPnt) pSet = CalculateChipFlutePointSet(iter);

        if (pSet.IsNull()) {
            std::cerr << "pSet is null" << std::endl;
            return -1;
        }

        if (!pSet->IsEmpty()) {
            //剔除多余点集
            // RemoveRedundantPointSet(pSets);

            // if (count == 0) {
            //     for (int i = 1; i <= pSet->Size(); i++) {
            //         lucViewerManager::SendMessage(pSet->Value(i),
            //                                       0.03,
            //                                       Quantity_NOC_YELLOW);
            //     }
            // }

            pSet = ProcessPointSet(pSet);

            pSets.emplace_back(pSet);

            //创建容屑槽
            ChipFlute* chipFlute = new ChipFlute();
            auto create_nurbs = [](Handle(TColgp_HSequenceOfPnt) pSet) {
                if (pSet->IsEmpty()) return TopoDS_Edge();
                if (pSet->Size() == 2)
                    return BRepBuilderAPI_MakeEdge(pSet->Value(1), pSet->Value(2)).Edge();

                Standard_Integer size = pSet->Size();
                TColgp_HArray1OfPnt pArray(1, size);
                for (int i = 1; i <= pSet->Size(); i++)
                    pArray.SetValue(i, pSet->Value(i));
                Standard_Integer degree = 3;
                Standard_Integer maxDeg = 8;
                Standard_Real tol = 0.002;
                GeomAPI_PointsToBSpline approx(pArray, degree, maxDeg, GeomAbs_C2, tol);
                TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(approx.Curve());
                return edge;
            };

            // 显示测试点集
            // if (count == 0) {
            //     for (int i = 1; i <= pSet->Size(); i++) {
            //         DisplayPoint(pSet->Value(i), 0.01, Quantity_NOC_RED);
            //     }
            //     count++;
            // }

            TopoDS_Edge edge = create_nurbs(pSet);

            // lucViewerManager::SendMessage(edge, Quantity_NOC_BLUE);

            chipFlute->flute = edge;
            chipFlute->pSet = pSet;

            std::cout << __FUNCTION__ << ": " << __LINE__ << std::endl;
            if (chipFlute == nullptr || chipFlute->flute.IsNull() ||
                chipFlute->pSet->IsEmpty())
                return -1;

            chipFlutes.emplace_back(chipFlute);
        } else {
            std::cerr << "Calculate chip flute point set failed!" << std::endl;
            return -1;
        }
    }

    write_file(pSets);

    //处理获得的容屑槽
    ProcessChipFlute(chipFlutes);

    for (lucMatrix4x4* iter : matrixes) {
        iter->Delete();
    }
    matrixes.clear();

    return 0;
}

std::vector<lucMatrix4x4*> lucChipFluteModel::CalculateGrindWheelPose()
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;

    std::vector<lucMatrix4x4*> matrixes = {};

    lucToolPath* toolPath = nullptr;
    if (m_fluteType == FluteType::MILL || m_fluteType == FluteType::BARREL) {
        std::cout << "--- fluteType: mill" << std::endl;
        lucCutterCylineFlute* lucFlute = lucCutterCylineFlute::New();
        lucFlute->SetCutterGeometricParameter(m_geomParam);
        m_proStepBase->SetGrindingWheel(m_gWheel);
        lucFeedrate* fRateTmp = lucFeedrate::New();
        m_proStepBase->SetFeedrate(fRateTmp);
        lucFlute->SetProcessStep(m_proStepBase);
        toolPath = lucFlute->GetOutput();

    } else if (m_fluteType == FluteType::WOODTOOL) {
        lucWoodToolFlute* woodFlute = lucWoodToolFlute::New();
        woodFlute->SetCutterGeometricParameter(m_woodParam);
        m_proStepBase->SetGrindingWheel(m_gWheel);
        lucFeedrate* fRateTmp = lucFeedrate::New();
        m_proStepBase->SetFeedrate(fRateTmp);
        woodFlute->SetProcessStep(m_proStepBase);

        toolPath = woodFlute->GetOutput();
    }

    if (toolPath == nullptr) {
        std::cerr << "failed to get toolPath." << std::endl;
        return matrixes;
    }

    // 刀轨扫掠测试
    {
        TopoDS_Shape blank =
            BRepPrimAPI_MakeCylinder(ax2OXY,
                                     m_geomParam->GetOutsideDiameter() / 2,
                                     m_geomParam->GetCuttingEdgeLength());
        // lucViewerManager::SendMessage(blank, Quantity_NOC_GRAY, AIS_WireFrame);

        // TopoDS_Shape grndWheel =
        //     BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, 0), gp::DX()),
        //                          m_gWheel->GetDiameter() / 2,
        //                          50 - 10 / tan(lucMath::RadiansFromDegrees(60.)),
        //                          m_gWheel->GetWidth());

        TopoDS_Shape grndWheel =
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DX()),
                                     m_gWheel->GetDiameter() / 2,
                                     m_gWheel->GetWidth());

        grndWheel = m_proStepBase->GetGrindingWheel()->GetCurrentShape();
        gp_Trsf tR;
        tR.SetRotation(ax1OY, M_PI_2);
        grndWheel = BRepBuilderAPI_Transform(grndWheel, tR);

        gp_Pnt p0(10., 0, 0);
        gp_Pnt p1(10, 50, 0);
        gp_Pnt p2(0, 50, 0);
        TopoDS_Edge edge01 = BRepBuilderAPI_MakeEdge(p0, p1);
        TopoDS_Edge edge12 = BRepBuilderAPI_MakeEdge(p1, p2);
        TopoDS_Edge edge2O = BRepBuilderAPI_MakeEdge(p2, pO);
        TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge01, edge12, edge2O);
        grndWheel = BRepPrimAPI_MakeRevol(wire, gp_Ax1(pO, gp::DX()));

        UnNamed un;
        grndWheel = m_gWheel->GetCurrentShape();
        TopoDS_Shape shape = un.CreateChipFlute(blank, grndWheel, toolPath);

        // GrindingWheelSweptVolumeLine gsvl;
        // gsvl.ParseFile();
        // gsvl.CreateSweptLine();
        // gsvl.CreateSweptShape();

        // GrindingWheelSweptVolume gsv(m_gWheel);
        // gsv.InitToolPath(toolPath);
        // gsv.CalculateEnvelopeLine();
    }

    Standard_Integer segNumber = toolPath->GetNumberOfSegs();
    if (segNumber != m_geomParam->GetTeethNumber()) {
        std::cerr << "Invalid seg number: " << segNumber << std::endl;
        return matrixes;
    }

    if (m_neededOptimized) {
        m_isOptimized = Standard_True;
        InitGrindingWheelEquation();
        m_isOptimized = Standard_False;
    }

    Standard_Integer index = 0;  //点和法向量对应的索引
    lucDoubleArray* normalsT = toolPath->GetNormals();

    //处理位姿变换矩阵
    auto calculate_matrix = [this](const Standard_Real* const point,
                                   const Standard_Real(&norm)[3]) {
        double DY[3] = {0., 0., 1.};
        double axis[3] = {};
        lucMath::Cross(DY, norm, axis);
        double lambda = acos(lucMath::Dot(DY, norm));
        lucMath::Normalize(axis);
        lucNew<lucMatrix4x4> K;
        double elementK[16] = {0.,
                               -axis[2],
                               axis[1],
                               0.,
                               axis[2],
                               0,
                               -axis[0],
                               0.,
                               -axis[1],
                               axis[0],
                               0,
                               0.,
                               0.,
                               0.,
                               0.,
                               1.};
        K->DeepCopy(elementK);
        PrintMatrix("K", K);

        lucNew<lucMatrix4x4> I;
        I->Identity();

        lucNew<lucMatrix4x4> R1, R2, KSquare;
        lucMatrix4x4::Multiply4x4(K, ScalarMatrix(sin(lambda)), R1);
        lucMatrix4x4::Multiply4x4(K, K, KSquare);
        lucMatrix4x4::Multiply4x4(KSquare, ScalarMatrix(1 - cos(lambda)), R2);
        PrintMatrix("R1", R1);
        PrintMatrix("R2", R2);
        PrintMatrix("KSquare", KSquare);

        double elementR[16] = {};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                elementR[4 * i + j] =
                    I->GetElement(i, j) + R1->GetElement(i, j) + R2->GetElement(i, j);
            }
        }
        // lucNew<lucMatrix4x4> R;
        lucMatrix4x4* R = lucMatrix4x4::New();
        R->DeepCopy(elementR);
        PrintMatrix("R", R);

        R->SetElement(0, 3, point[0]);
        R->SetElement(1, 3, point[1]);
        R->SetElement(2, 3, point[2]);
        R->SetElement(3, 3, 1.);
        // PrintMatrix("R", R);

        return R;
        // matrixes.emplace_back(R);
    };

    //获取点、法向量和旋转变换矩阵，砂轮坐标中X为轴向，需要变换
    // Output中的法向量方向与算法中砂轮法向量方向相反
    for (Standard_Integer i = 0; i < segNumber; i++) {
        lucSeg* seg = toolPath->GetSeg(i);
        lucPoints* points = seg->GetPoints();

        //获取第一个位姿
        //获取点
        Standard_Real* p_tmp = points->GetPoint(0);

        std::cout << "---modeling flute point(" << p_tmp[0] << ", " << p_tmp[1] << ", "
                  << p_tmp[2] << ")" << std::endl;

        Standard_Real point[3] = {p_tmp[1], p_tmp[2], p_tmp[0]};
        std::cout << "pointTmp(" << p_tmp[0] << ", " << p_tmp[1] << ", " << p_tmp[2]
                  << ")" << std::endl;
        std::cout << "point(" << point[0] << ", " << point[1] << ", " << point[2] << ")"
                  << std::endl;
        // std::copy(point, point + 3, O5 + i * 3);

        m_gZ = point[2];

        //获取点对应的法向量
        Standard_Real norm_tmp[3] = {0};
        normalsT->GetTuple(index, norm_tmp);
        Standard_Real norm[3] = {norm_tmp[1], norm_tmp[2], norm_tmp[0]};
        std::cout << "norm_tmp[i"
                  << ": " << norm_tmp[0] << ", " << norm_tmp[1] << ", " << norm_tmp[2]
                  << std::endl;
        std::cout << "normal[i"
                  << ": " << norm[0] << ", " << norm[1] << ", " << norm[2] << std::endl;

        //计算位姿变换矩阵
        PrintMatrix("emplace back", calculate_matrix(point, norm));
        matrixes.emplace_back(calculate_matrix(point, norm));

        //更新索引
        if (i == segNumber - 1) continue;
        Standard_Integer n = seg->GetNumberOfPoints();
        index += n;

//显示砂轮
#if 0
        if (i == 0) {
            gp_Pnt pCenter1(p_tmp[0], p_tmp[1], p_tmp[2]);
            gp_Vec vecCenter1(norm_tmp[0], norm_tmp[1], norm_tmp[2]);
            gp_Pnt pCenter(point[0], point[1], point[2]);
            gp_Vec vecCenter(norm[0], norm[1], norm[2]);
            std::cout << "pCenter(" << pCenter.X() << ", " << pCenter.Y() << ", "
                      << pCenter.Z() << ")" << std::endl;
            std::cout << "vecCenter[i"
                      << ": " << vecCenter.X() << ", " << vecCenter.Y() << ", "
                      << vecCenter.Z() << std::endl;
            gp_Ax2 ax2(pCenter, vecCenter);
            gp_Ax2 ax21(pCenter1, vecCenter1);
            std::cout << "d1: " << m_gWheel->GetDiameter() << "d2: " << m_gWheel->GetDiameter_Littleende() << "w: " << m_gWheel->GetWidth()<< std::endl;
            // TopoDS_Shape grind = BRepPrimAPI_MakeCone(ax2, m_gWheel->GetDiameter_Littleende() / 2, m_gWheel->GetDiameter() / 2, m_gWheel->GetWidth());
            TopoDS_Shape grind = BRepPrimAPI_MakeCone(ax2,  98.02 / 2, 94.10 / 2, 10.0);
            // TopoDS_Shape grind = BRepPrimAPI_MakeCylinder(ax2, 150 / 2, 10);
            lucViewerManager::SendMessage(grind, Quantity_NOC_BLUE, AIS_WireFrame);

            // TopoDS_Shape cyl1 = BRepPrimAPI_MakeCylinder(gp_Ax2(pO, gp::DX()), 5., 100.);
            TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(ax2OZX, 15./2, 100.);
            lucViewerManager::SendMessage(cyl);
            // lucViewerManager::SendMessage(cyl1, Quantity_NOC_BLACK);

            gp_Pnt pVec = pCenter.Translated(vecCenter * 50);
            lucViewerManager::SendMessage(BRepBuilderAPI_MakeEdge(pCenter, pVec),
                                          Quantity_NOC_BLUE);
            // return std::vector<lucMatrix4x4*>();
        }
#endif
    }

    for (int i = 0; i < matrixes.size(); i++) {
        PrintMatrix("Get matrix", matrixes[i]);
    }

    return matrixes;
}

//计算构成容屑槽的点集
Handle(TColgp_HSequenceOfPnt)
    lucChipFluteModel::CalculateChipFlutePointSet(const lucMatrix4x4* matrix)
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;

    PrintMatrix("matrix", matrix);
    Standard_Real A[4][4] = {};
    std::copy(matrix->GetData(), matrix->GetData() + 16, &A[0][0]);

    Standard_Real len = m_geomParam->GetCuttingEdgeLength();
    std::cout << "cutting edge length: " << len << std::endl;

    Standard_Real T = 0.5 * M_PI;
    std::mutex mtx_t;

    auto calculate_points_t = [&, this](std::vector<coord_>& coordVec,
                                        const Standard_Real z,
                                        const Standard_Integer k,
                                        const Standard_Real& deltaT) {
        Standard_Real t = k * deltaT;
        std::vector<coord_> coordVecTmp;
        for (int j = 0; j < m_gEquations.size(); j++) {
            Standard_Real M1 = m_gRanges[j].second;
            // Standard_Real deltaM1 = 0.05;
            Standard_Real deltaM1 = 0.01;  // mark
            for (Standard_Real m1 = m_gRanges[j].first; m1 < M1 + deltaM1;
                 m1 += deltaM1) {
                if (m1 > M1) m1 = M1;
                Standard_Real result[3] = {0.};
                coord_ coord = SolveNewton2D(m1, t, z, m_gEquations[j](m1), A);
                if (std::get<2>(coord) == 0) {
                    // std::lock_guard<std::mutex> lock(mtx_t);
                    // coordVec.emplace_back(coord);
                    coordVecTmp.emplace_back(coord);
                }
            }
        }
        std::lock_guard<std::mutex> lock(mtx_t);
        coordVec.insert(coordVec.end(), coordVecTmp.begin(), coordVecTmp.end());
    };

    // CALCULATE_TIME_START;

    Standard_Real z = 0;
    Handle(TColgp_HSequenceOfPnt) pSet = new TColgp_HSequenceOfPnt();
    std::vector<coord_> coordVec;

    // if (m_isStraight == Standard_False) {
    if (1) {
        std::counting_semaphore<16> tSemaphore(16);
        std::vector<std::thread> threadsT;
        // Standard_Integer nT = std::abs(T / 0.01);
        Standard_Integer nT = std::abs(T / 0.001);  // mark
        if (m_rBlade > 10) {
            nT = std::abs(T / 0.002);
        }
        Standard_Real deltaT = T / nT;

        if (m_isStraight) {
            nT = m_gWheel->GetDiameter();
            deltaT = 1;
        }

#if 1
        std::cout << "--- Calculate chip flute point set ---" << std::endl;
        for (Standard_Integer k = -nT; k <= nT; k++) {
            if (tSemaphore.try_acquire_for(std::chrono::seconds(5))) {
                threadsT.push_back(std::thread([&, z, k, deltaT]() {
                    try {
                        calculate_points_t(coordVec, z, k, deltaT);
                    } catch (const std::exception& e) {
                        std::cerr << "Exception caught: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "Unknown exception caught" << std::endl;
                    }
                    tSemaphore.release();
                }));
            } else {
                std::cerr << "Timeout waiting for semaphore" << std::endl;
            }
        }
        for (auto& th : threadsT) {
            th.join();
        }
    } else {
        std::cout << "--- is straight ---" << std::endl;
        calculate_points_t(coordVec, m_gZ, 0., 0.);
    }
    // CALCULATE_TIME_END;
#else
        for (Standard_Integer k = -nT; k <= nT; k++) {
            calculate_points_t(coordVec, z, k, deltaT);
        }
#endif

    std::cout << "get coordVec size: " << coordVec.size() << std::endl;
    for (const auto& coord : coordVec) {
        gp_Pnt pnt(std::get<0>(coord), std::get<1>(coord), std::get<2>(coord));
        pSet->Append(pnt);
    }

    return pSet;
}

coord_ lucChipFluteModel::SolveNewton2D(const Standard_Real& m1, const Standard_Real& t,
                                        const Standard_Real& z, const Standard_Real& fg,
                                        const Standard_Real (&A)[4][4])
{
    // Standard_Real lambda = (A[2][3] * 1) / lead * (2 * M_PI);
    Standard_Real lambda = 0;

    auto mxQt_expr = [lambda, A, m1, fg, t, this](const Standard_Real& m2) {
        if (m_isStraight) {
            return 1 * (A[0][3] + A[0][2] * m1 + A[0][0] * fg * cos(m2) +
                        A[0][1] * fg * sin(m2)) -
                   0 * (A[1][3] + A[1][2] * m1 + A[1][0] * fg * cos(m2) +
                        A[1][1] * fg * sin(m2));
        } else {
            return cos(t - lambda) * (A[0][3] + A[0][2] * m1 + A[0][0] * fg * cos(m2) +
                                      A[0][1] * fg * sin(m2)) -
                   sin(t - lambda) * (A[1][3] + A[1][2] * m1 + A[1][0] * fg * cos(m2) +
                                      A[1][1] * fg * sin(m2));
        }
    };
    auto myQt_expr = [lambda, A, m1, fg, t, this](const Standard_Real& m2) {
        if (m_isStraight) {
            return 1 * (A[1][3] + A[1][2] * m1 + A[1][0] * fg * cos(m2) +
                        A[1][1] * fg * sin(m2)) +
                   0 * (A[0][3] + A[0][2] * m1 + A[0][0] * fg * cos(m2) +
                        A[0][1] * fg * sin(m2));
        } else {
            return cos(t - lambda) * (A[1][3] + A[1][2] * m1 + A[1][0] * fg * cos(m2) +
                                      A[1][1] * fg * sin(m2)) +
                   sin(t - lambda) * (A[0][3] + A[0][2] * m1 + A[0][0] * fg * cos(m2) +
                                      A[0][1] * fg * sin(m2));
        }
    };
    auto F = [lambda, A, m1, fg, t, z, this](const Standard_Real& m2) {
        if (m_isStraight) {
            return A[2][3] + A[2][2] * m1 + A[2][0] * fg * cos(m2) +
                   A[2][1] * fg * sin(m2) + t * 0.5 - z;
        } else {
            return A[2][3] + A[2][2] * m1 + A[2][0] * fg * cos(m2) +
                   A[2][1] * fg * sin(m2) + (m_lead * (t - lambda)) / (2 * M_PI) - z;
        }
    };

    auto dF_dm2 = [F](const Standard_Real& m2) {
        Standard_Real h = 1e-4;
        return (F(m2 + h) - F(m2 - h)) / (2 * h);
    };

    // onst Standard_Real mR = m_geomParam->GetOutsideDiameter() / 2;
    const Standard_Real epsilon = 1e-7;
    const Standard_Real M2 = 2 * M_PI;
    const Standard_Real deltaM2 = M2 / 10;
    std::array<Standard_Real, 6> m2Array = {1., 2., 3., 4., 5., 6.};

    for (Standard_Integer i = 0; i < m2Array.size(); i++) {
        for (double m2_ = m2Array[i]; m2_ <= M2; m2_ += deltaM2) {
            double m2 = m2_;
            // 牛顿法迭代求解
            int iterationCount = 0;
            while (iterationCount < 100) {
                double fVal = F(m2);
                double df = dF_dm2(m2);

                if (std::abs(df) < 1e-10) {
                    // 防止除以零，跳过此迭代
                    continue;
                }
// 显示砂轮
#if 0
                if (t == 0 || t == 2) {
                    if (m1 == 0 || m1 == 4 || m1 == 8 || m1 == 10) {
                        double Z = A[2][3] + A[2][2] * m1 + A[2][0] * fg * cos(m2) +
                                   A[2][1] * fg * sin(m2) +
                                   (m_lead * (t - lambda)) / (2 * M_PI);
                        lucViewerManager::SendMessage(
                            gp_Pnt(mxQt_expr(m2), myQt_expr(m2), Z),
                            0.03,
                            Quantity_NOC_YELLOW);
                    }
                }
#endif

                if (std::abs(fVal) < epsilon) {
                    // 已经接近解，返回结果
                    double x = mxQt_expr(m2);
                    double y = myQt_expr(m2);

                    if (x * x + y * y <= m_rBlade * m_rBlade) {
                        return {x, y, z};
                    }

                    break;
                }

                // 更新 m2 的值
                m2 -= fVal / df;
                iterationCount++;
            }
        }
    }
    //没有找到合适的解
    return {-1., -1., -1.};
}

void lucChipFluteModel::RemoveRedundantPointSet(
    std::vector<Handle(TColgp_HSequenceOfPnt)>& pSets)
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;

    Standard_Integer turningIdx = 0;
    Standard_Integer windSize = 5;  //至少连续4次下降
    Standard_Integer n = pSets.size();
    std::cout << "pSets size: " << n << std::endl;
    for (int i = 0; i <= n - windSize; ++i) {
        Standard_Boolean decreasing = Standard_True;
        for (int j = 0; j < windSize - 1; ++j) {
            if (pSets[i + j + 1]->Size() >= pSets[i + j]->Size()) {
                decreasing = Standard_False;
                break;
            }
        }
        if (decreasing) {
            turningIdx = i;
            break;
        }
    }
    std::cout << "Get turning point idx: " << turningIdx
              << " z: " << pSets[turningIdx]->Value(1).Z() << std::endl;

    if (turningIdx > 0) pSets.erase(pSets.begin(), pSets.begin() + turningIdx);

    //将Z轴坐标修改为从0开始
    Standard_Real z0 = pSets[0]->Value(1).Z();
    for (auto& pSet : pSets) {
        for (int i = 1; i <= pSet->Size(); i++) {
            Standard_Real z = pSet->Value(i).Z();
            pSet->ChangeValue(i).SetZ(z - z0);
        }
    }

    for (int i = 0; i < pSets.size(); i++) {
        std::cout << "After erase pSets[" << i << "]: " << pSets[i]->Size() << std::endl;
    }
}

Handle(TColgp_HSequenceOfPnt)
    lucChipFluteModel::ProcessPointSet(Handle(TColgp_HSequenceOfPnt) pSet)
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;
    //坐标点过少，不做处理
    if (pSet->Size() <= 2) return pSet;
    // for(int i = 1; i <= pSet->Size(); i++) {
    //     const gp_Pnt& p = pSet->Value(i);
    //     double dis = p.Distance(gp_Pnt(0, 0, p.Z()));
    //     if(dis - m_geomParam->GetOutsideDiameter() / 2 < 5) {
    //         pSet->Remove(i);
    //     }
    // }

    gp_Pnt pO(0, 0, 0);
    // Standard_Real mR = m_geomParam->GetOutsideDiameter() / 2;
    Standard_Integer number = m_rBlade * 30;  //圆环数量
    Standard_Real delta_mR = m_rBlade / number;
    std::vector<std::vector<gp_Pnt>> ring(number + 1);
    //分配圆环
    for (int i = 1; i <= pSet->Size(); i++) {
        Standard_Real x = pSet->Value(i).X();
        Standard_Real y = pSet->Value(i).Y();
        Standard_Real z = pSet->Value(i).Z();
        Standard_Real dis = std::sqrt(x * x + y * y);
        Standard_Integer index = dis / delta_mR;
        if (index <= number) ring[index].emplace_back(gp_Pnt(x, y, z));
    }
    //显示圆环
    for (int i = 1; i < number; i++) {
        gp_Pnt C(0, 0, 0);
        gp_Circ circ(gp_Ax2(C, gp::DZ()), i * delta_mR);
        Handle(Geom_Circle) gC = GC_MakeCircle(circ);
        TopoDS_Edge eC = BRepBuilderAPI_MakeEdge(gC);
    }
    std::vector<gp_Pnt> pSet2;
    std::vector<gp_Pnt> pSet3;
    for (auto& points : ring) {
        //根据与Y轴夹角大小排序，确保iter[0]位置
        std::sort(points.begin(), points.end(), [pO](const gp_Pnt& p1, const gp_Pnt& p2) {
            gp_Vec vec1(pO, p1);
            gp_Vec vec2(pO, p2);
            return vec1.Angle(gp::DY()) > vec2.Angle(gp::DY());
        });
        gp_Pnt p2, p3;
        if (points.empty())
            continue;
        else if (points.size() == 1) {
            pSet2.emplace_back(points[0]);
            continue;
        } else if (points.size() == 2) {
            //环内有两个点
            p2 = gp_Pnt(points[0].X(), points[0].Y(), points[0].Z());
            p3 = gp_Pnt(points[1].X(), points[1].Y(), points[1].Z());
            pSet2.emplace_back(p2);
            pSet3.emplace_back(p3);
            continue;
        } else {
            gp_Pnt p1(points[0].X(), points[0].Y(), points[0].Z());
            Standard_Real dis1 = 0.;
            Standard_Real dis2 = 0.;
            for (auto& p : points) {
                Standard_Real dis = std::sqrt((p.X() - p1.X()) * (p.X() - p1.X()) +
                                              (p.Y() - p1.Y()) * (p.Y() - p1.Y()));
                if (dis > dis1) {
                    dis1 = dis;
                    p2 = gp_Pnt(p.X(), p.Y(), p.Z());  // p2为圆环中与p1距离最远点
                }
            }
            for (auto& p : points) {
                Standard_Real dis = std::sqrt((p.X() - p2.X()) * (p.X() - p2.X()) +
                                              (p.Y() - p2.Y()) * (p.Y() - p2.Y()));
                if (dis > dis2) {
                    dis2 = dis;
                    p3 = gp_Pnt(p.X(), p.Y(), p.Z());  // p3为圆环中与p2距离最远点
                }
            }
            pSet2.emplace_back(p2);
            pSet3.emplace_back(p3);
        }
    }
    if (pSet2.empty() || pSet3.empty()) return Handle(TColgp_HSequenceOfPnt)();
    //将p2，p3按与原点距离排序
    std::sort(pSet2.begin(), pSet2.end(), [](const gp_Pnt& p2_1, const gp_Pnt& p2_2) {
        Standard_Real dis1 = p2_1.X() * p2_1.X() + p2_1.Y() * p2_1.Y();
        Standard_Real dis2 = p2_2.X() * p2_2.X() + p2_2.Y() * p2_2.Y();
        return dis1 > dis2;
    });
    std::sort(pSet3.begin(), pSet3.end(), [](const gp_Pnt& p3_1, const gp_Pnt& p3_2) {
        Standard_Real dis1 = p3_1.X() * p3_1.X() + p3_1.Y() * p3_1.Y();
        Standard_Real dis2 = p3_2.X() * p3_2.X() + p3_2.Y() * p3_2.Y();
        return dis1 < dis2;
    });
//剔除距离过近的点
// mark
#if 0
    Standard_Real tol = m_rBlade / 20;
    for (Standard_Integer i = 1; i < pSet2.size();) {
        gp_Pnt p0 = pSet2[i - 1];
        gp_Pnt p1 = pSet2[i];
        if (p0.Distance(p1) < tol)
            pSet2.erase(pSet2.begin() + i);
        else
            ++i;
    }
    for (Standard_Integer i = 1; i < pSet3.size();) {
        gp_Pnt p0 = pSet3[i - 1];
        gp_Pnt p1 = pSet3[i];
        if (p0.Distance(p1) < tol)
            pSet3.erase(pSet3.begin() + i);
        else
            ++i;
    }
#endif

    std::vector<gp_Pnt> pSetSortedTmp = pSet2;
    pSetSortedTmp.insert(pSetSortedTmp.end(), pSet3.begin(), pSet3.end());
    CompletePointSet(pSetSortedTmp);

    //按距离排序
    lucCutterModelAlgorithm::SortChipFlutePointSet(pSetSortedTmp);

    //按顺序重新构建点集
    Handle(TColgp_HSequenceOfPnt) pSetSorted = new TColgp_HSequenceOfPnt;
    for (Standard_Size i = 0; i < pSetSortedTmp.size(); i++) {
        // std::cout << "pSetSorted[" << i << "]: (" << pSetSortedTmp[i].X() << " " <<
        // pSetSortedTmp[i].Y() << " " << pSetSortedTmp[i].Z() << ")" << std::endl;
        pSetSorted->Append(pSetSortedTmp[i]);
    }

    return pSetSorted;
}

void lucChipFluteModel::CompletePointSet(std::vector<gp_Pnt>& pSet)
{
    Standard_Real size = pSet.size();
    if (size < 4) return;

    // Standard_Real rBlade = m_geomParam->GetOutsideDiameter() / 2;
    gp_Pnt pS = pSet[0];
    gp_Pnt pL = pSet[size - 1];
    gp_Pnt pS1 = pSet[1];
    gp_Pnt pL1 = pSet[size - 2];
    gp_Pnt pntOZ(0, 0, pS.Z());
    Standard_Real dis1 = pS.Distance(pntOZ);
    Standard_Real dis2 = pL.Distance(pntOZ);

    gp_Pnt pOt(0, 0, pS.Z());

    gp_Circ circ(gp_Ax2(pOt, gp::DZ()), m_rBlade);
    Handle(Geom_Circle) cC = GC_MakeCircle(circ);

    if (std::abs(dis1 - m_rBlade) > Precision::Confusion()) {
        gp_Vec vecS(pS1, pS);
        gp_Pnt pS2 = pS.Translated(vecS.Normalized() * m_rBlade);
        Handle(Geom_TrimmedCurve) cS = GC_MakeSegment(pS, pS2);
        gp_Pnt pC;
        if (lucCutterModelAlgorithm::GetNearestPoint(cS, cC, pC) == 0) {
            pSet.insert(pSet.begin(), pC);
        }
    }
    if (std::abs(dis2 - m_rBlade) > Precision::Confusion()) {
        gp_Vec vecL(pL1, pL);
        gp_Pnt pL2 = pL.Translated(vecL.Normalized() * m_rBlade);
        Handle(Geom_TrimmedCurve) cL = GC_MakeSegment(pL, pL2);
        gp_Pnt pC;
        if (lucCutterModelAlgorithm::GetNearestPoint(cL, cC, pC) == 0) {
            pSet.emplace_back(pC);
        }
    }

    return;
}

void lucChipFluteModel::ProcessChipFlute(std::vector<ChipFlute*>& chipFlutes)
{
    std::cout << "Enter function: " << __FUNCTION__ << ": " << std::endl;

    Standard_Size fluteSize = chipFlutes.size();
    Standard_Real angleToX = 0.;
    for (Standard_Size i = 0; i < fluteSize; i++) {
        // pS(pB)为第一个点，pL为最后一个点
        Standard_Size n = chipFlutes[i]->pSet->Size();
        gp_Pnt pSTmp = chipFlutes[i]->pSet->Value(1);
        gp_Pnt pLTmp = chipFlutes[i]->pSet->Value(n);
        gp_Pnt pB;  // pS
        gp_Pnt pL;
        gp_Pnt pSNxt;
        gp_Pnt pLNxt;
        Standard_Size iNxt;
        if (i == fluteSize - 1)
            iNxt = 0;
        else
            iNxt = i + 1;

        Standard_Size nNext = chipFlutes[iNxt]->pSet->Size();
        if (m_geomParam->GetTeethNumber() == 2) {
            //当齿数为2时，暂时只能以点到Y轴的距离判断起始点和终点
            gp_Pnt pSNextTmp = chipFlutes[iNxt]->pSet->Value(1);
            gp_Pnt pLNextTmp = chipFlutes[iNxt]->pSet->Value(nNext);
            Standard_Real dis1 = std::abs(pSTmp.X());
            Standard_Real dis2 = std::abs(pLTmp.X());
            Standard_Real dis3 = std::abs(pSNextTmp.X());
            Standard_Real dis4 = std::abs(pLNextTmp.X());
            auto swap_point = [](bool condition, gp_Pnt& p1, gp_Pnt& p2) {
                if (!condition) {
                    gp_Pnt pTmp = p1;
                    p1 = p2;
                    p2 = pTmp;
                }
            };
            swap_point(dis1 > dis2, pSTmp, pLTmp);
            swap_point(dis3 > dis4, pSNextTmp, pLNextTmp);

            pB = pLTmp;
            pL = pSTmp;
            pSNxt = pLNextTmp;
            pLNxt = pSNextTmp;

        } else {
            gp_Pnt pSNextTmp = chipFlutes[iNxt]->pSet->Value(1);
            gp_Pnt pLNextTmp = chipFlutes[iNxt]->pSet->Value(nNext);
            Standard_Real dis1 = pSTmp.SquareDistance(pSNxt);
            Standard_Real dis2 = pLTmp.SquareDistance(pSNxt);
            Standard_Real minDis = std::numeric_limits<Standard_Real>::max();
            gp_Pnt p0[2] = {pSTmp, pLTmp};
            gp_Pnt p1[2] = {pSNextTmp, pLNextTmp};

            for (Standard_Size i0 = 0; i0 < 2; i0++) {
                for (Standard_Size i1 = 0; i1 < 2; i1++) {
                    if (Standard_Real dis = p0[i0].SquareDistance(p1[i1]); dis < minDis) {
                        minDis = dis;
                        pB = p0[i0], pL = p0[1 - i0];
                        pLNxt = p1[i1], pSNxt = p1[1 - i1];
                    }
                }
            }
        }

        if (i == 0) {
            gp_Vec vecOB(pO, pB);
            gp_Vec vecOX(1, 0, 0);
            angleToX = -vecOB.Angle(vecOX);
            std::cout << "angleToX: " << lucMath::DegreesFromRadians(angleToX)
                      << std::endl;
        }
        //将所有容屑槽旋转至第一容屑槽刀尖点位于X轴位置
        gp_Trsf tR;
        tR.SetRotation(ax1OZ, angleToX);
        TopoDS_Edge fluteX =
            TopoDS::Edge(BRepBuilderAPI_Transform(chipFlutes[i]->flute, tR));
        chipFlutes[i]->flute = fluteX;
        chipFlutes[i]->pStart = pB.Transformed(tR);
        chipFlutes[i]->pEnd = pL.Transformed(tR);

        /*左切*/
        if (m_geomParam->GetCuttingDirection() ==
            lucCutterGeometricParameter::Direction::Left) {
            gp_Vec vecOS(pO, chipFlutes[i]->pStart);
            gp_Ax1 ax1(pO, vecOS);
            tR.SetRotation(ax1, M_PI);
            chipFlutes[i]->flute = TopoDS::Edge(BRepBuilderAPI_Transform(fluteX, tR));
            chipFlutes[i]->pStart = chipFlutes[i]->pStart.Transformed(tR);
            chipFlutes[i]->pEnd = chipFlutes[i]->pEnd.Transformed(tR);
        }
    }

    /*左切，反转容屑槽顺序*/
    if (m_geomParam->GetCuttingDirection() ==
        lucCutterGeometricParameter::Direction::Left) {
        if (chipFlutes.size() > 2) {
            std::reverse(chipFlutes.begin() + 1, chipFlutes.end());
        }
    }
}

//创建4*4标量矩阵
lucSmartPointer<lucMatrix4x4> lucChipFluteModel::ScalarMatrix(const double& k)
{
    double element[16] = {};
    for (int i = 0; i < 16; i++) {
        if (i % 5 == 0)
            element[i] = k;
        else
            element[i] = 0.;
    }
    lucNew<lucMatrix4x4> m;
    m->DeepCopy(element);
    return m;
}

void lucChipFluteModel::PrintMatrix(std::string name, const lucMatrix4x4* const m)
{
    std::cout << name << std::endl;
    for (int i = 0; i < 4; i++) {
        std::cout << " | " << m->GetElement(i, 0) << " " << m->GetElement(i, 1) << " "
                  << m->GetElement(i, 2) << " " << m->GetElement(i, 3) << " | "
                  << std::endl;
    }
    std::cout << "" << std::endl;
}

Standard_Integer lucChipFluteModel::CreateSimplifiedChipFlute2(
    std::vector<SimplifiedChipFlute>& chipFlutes,
    lucSmartPointer<lucCutterGeometricParameter> geomParam,
    lucSmartPointer<lucCutterProcess> processParam)
{
    std::cout << "Enter function: " << __FUNCTION__ << std::endl;
    m_geomParam = lucCutterGeometricParameter::New();
    m_processParam = lucCutterProcess::New();
    m_geomParam->DeepCopy(geomParam);
    m_processParam->DeepCopy(processParam);

    m_geomParam = lucCutterGeometricParameter::New();
    m_processParam = lucCutterProcess::New();
    m_geomParam->DeepCopy(geomParam);
    m_processParam->DeepCopy(processParam);

    std::vector<lucCutterProcessStepBase*> stepBaseVec = processParam->GetProcessSteps();

    Standard_Real angleG = M_PI_2;  //简化模型默认使用1A1砂轮磨削

    m_fluteParam = lucSmartPointer<lucCutterFluteGeometricParameter>::New();
    m_proStepBase = lucSmartPointer<lucCutterProcessStepBase>::New();
    for (Standard_Integer index = 0; index < stepBaseVec.size(); index++) {
        int opType = stepBaseVec[index]->GetOperationType();
        if (opType == lucProcessStepType::Operation::Flute ||
            opType == lucProcessStepType::Operation::WoodToolFlute ||
            opType == lucProcessStepType::Operation::BarrelFlute ||
            opType == lucProcessStepType::Operation::FormFlute) {
            m_fluteParam->DeepCopy(stepBaseVec[index]->GetProcessGeometricParameter());
            m_gWheel = stepBaseVec[index]->GetGrindingWheel();
            if (m_gWheel == nullptr) {
                std::cout << "---m_gWheel is null, use default grinding wheel"
                          << std::endl;
                InitDefaultGrindingWheel();
            } else {
                std::cout << "---m_gWheel is not null" << std::endl;
                angleG = M_PI_2 - lucMath::RadiansFromDegrees(m_gWheel->GetAngle());
                if (angleG >= M_PI_2 || angleG <= 0 ||
                    m_gWheel->GetType() != GrindingWheel::PERIPHERY) {
                    angleG = M_PI_2;
                }
            }

            // 强制优化砂轮位姿为false
            // m_fluteParam->SetIsArcProject(Standard_False);
            m_proStepBase->DeepCopy(stepBaseVec[index]);
            m_proStepBase->SetProcessGeometricParameter(m_fluteParam);
        }
    }

    Standard_Real rBlade = geomParam->GetOutsideDiameter() / 2;
    Standard_Real rCore = geomParam->GetCoreDiameter() / 2;

    Standard_Integer cutFlag = 1;
    if (m_geomParam->GetCuttingDirection() ==
        lucCutterGeometricParameter::Direction::Left)
        cutFlag = -1;

    Standard_Real gamma = m_fluteParam->GetRakeAngleS();
    gamma = 0;

    Standard_Real rFillet = m_gWheel->GetFillet();
    // if (rFillet >= (rBlade - rCore)) rFillet = (rBlade - rCore) * 0.25;
    if (rFillet >= (rBlade - rCore)) {
        std::cerr << "Invalid rFillet parameter" << std::endl;
        return -1;
    }

    /*计算各点*/
    auto create_flute_edge = [=, &chipFlutes](const Standard_Real& rotateAngle) {
        gp_Pnt pB(0, rBlade, 0);
        Standard_Real angleBAO = M_PI - asin(rBlade / rCore * sin(gamma));
        if (std::isnan(angleBAO)) {
            std::cerr << "Invalid angle parameter" << std::endl;
            return -1;
        }
        Standard_Real angleXOA = 0.5 * M_PI + M_PI - gamma - angleBAO;
        gp_Pnt pA(rCore * cos(angleXOA), rCore * sin(angleXOA), 0);

        /*将AB转至X轴*/
        pB = gp_Pnt(pB.Y(), -pB.X(), pB.Z());
        pA = gp_Pnt(pA.Y(), -pA.X(), pA.Z());

        gp_Vec vecAB(pA, pB);
        gp_Pnt pC = pA.Translated(vecAB.Normalized() * rBlade * 2);
        gp_Pnt pC0Tmp = pC.Rotated(gp_Ax1(pA, gp::DZ()), -angleG);

        Handle(Geom_Circle) circOut = GC_MakeCircle(ax2OZX, rBlade);
        Handle(Geom_TrimmedCurve) curveAC0Tmp = GC_MakeSegment(pA, pC0Tmp);
        gp_Pnt pC0;
        if (lucCutterModelAlgorithm::IsCurveIntesec(circOut, curveAC0Tmp, pC0) != 0) {
            ERROR;
            return -1;
        }
        gp_Pnt pC1(pC0.X(), -pC0.Y(), pC0.Z());

        Handle(Geom_TrimmedCurve) curveC0C1 = GC_MakeSegment(pC0, pC1);
        Handle(Geom_TrimmedCurve) curveBA = GC_MakeSegment(pB, pA);

        gp_Pnt pI;
        if (gamma == 0) {
            // gamma为0的时候判断相交失败？
            pI.SetCoord(rCore, 0, 0);
        } else {
            if (lucCutterModelAlgorithm::IsCurveIntesec(curveC0C1, curveBA, pI) != 0) {
                std::cerr << "Invalid curve intersection" << std::endl;
                return -1;
            }
        }

        if (rFillet > 0) {
            gp_Vec vecIB(pI, pB);
            gp_Vec vecIC0(pI, pC0);
            gp_Vec bisectorVec = vecIB.Normalized() + vecIC0.Normalized();
            Standard_Real angleC0IB =
                acos(vecIC0.Dot(vecIB) / (vecIC0.Magnitude() * vecIB.Magnitude()));
            if (std::isnan(angleC0IB)) {
                std::cerr << "Invalid angle parameter" << std::endl;
                return -1;
            }
            Standard_Real disIOf = rFillet / sin(angleC0IB / 2);
            gp_Pnt pOf = pI.Translated(bisectorVec.Normalized() * disIOf);  // 圆角圆心Of
            gp_Circ circF(gp_Ax2(pOf, gp::DZ()), rFillet);
            Handle(Geom_Circle) geomCircF = new Geom_Circle(circF);
            gp_Pnt pM, pN;  // 圆角与BI，C0I相切与M、N
            Handle(Geom_TrimmedCurve) curveBI = GC_MakeSegment(pB, pI);
            Handle(Geom_TrimmedCurve) curveC0I = GC_MakeSegment(pC0, pI);
            if (lucCutterModelAlgorithm::IsCurveIntesec(curveBI, geomCircF, pM) != 0 ||
                lucCutterModelAlgorithm::IsCurveIntesec(curveC0I, geomCircF, pN) != 0) {
                std::cerr << "Invalid curve intersection" << std::endl;
                return -1;
            }

            gp_Vec vecOfM(pOf, pM);
            gp_Vec vecOfN(pOf, pN);
            gp_Vec vecOfQ = vecOfM.Added(vecOfN);
            gp_Pnt pQ = pOf.Translated(vecOfQ.Normalized() * rFillet);

            if (cutFlag == -1) {
                pM.SetY(-pM.Y());
                pN.SetY(-pN.Y());
                pC0.SetY(-pC0.Y());
                pQ.SetY(-pQ.Y());
            }

            Handle(Geom_TrimmedCurve) curveMN = GC_MakeArcOfCircle(pM, pQ, pN);
            TopoDS_Edge edgeBM = BRepBuilderAPI_MakeEdge(pB, pM);
            TopoDS_Edge edgeMN = BRepBuilderAPI_MakeEdge(curveMN);
            TopoDS_Edge edgeNC0 = BRepBuilderAPI_MakeEdge(pN, pC0);

            gp_Trsf tR;
            tR.SetRotation(ax1OZ, rotateAngle);
            edgeBM = TopoDS::Edge(BRepBuilderAPI_Transform(edgeBM, tR));
            edgeMN = TopoDS::Edge(BRepBuilderAPI_Transform(edgeMN, tR));
            edgeNC0 = TopoDS::Edge(BRepBuilderAPI_Transform(edgeNC0, tR));
            pB = pB.Rotated(ax1OZ, rotateAngle);
            pC0 = pC0.Rotated(ax1OZ, rotateAngle);
            pN = pN.Rotated(ax1OZ, rotateAngle);

            SimplifiedChipFlute chipFlute;
            chipFlute.edges = {edgeBM, edgeMN, edgeNC0};
            chipFlute.pStart = pB;
            chipFlute.pEnd = pC0;
            chipFlute.pStartLastEdge = pN;
            chipFlutes.emplace_back(chipFlute);

            return 0;
        } else if (rFillet == 0) {
            std::cout << "---rFillet == 0" << std::endl;
            if (cutFlag == -1) {
                pI.SetY(-pI.Y());
                pC0.SetY(-pC0.Y());
            }
            TopoDS_Edge edgeBI = BRepBuilderAPI_MakeEdge(pB, pI);
            TopoDS_Edge edgeIC0 = BRepBuilderAPI_MakeEdge(pI, pC0);

            gp_Trsf tR;
            tR.SetRotation(ax1OZ, rotateAngle);
            edgeBI = TopoDS::Edge(BRepBuilderAPI_Transform(edgeBI, tR));
            edgeIC0 = TopoDS::Edge(BRepBuilderAPI_Transform(edgeIC0, tR));
            pB = pB.Rotated(ax1OZ, rotateAngle);
            pC0 = pC0.Rotated(ax1OZ, rotateAngle);
            pI = pI.Rotated(ax1OZ, rotateAngle);

            SimplifiedChipFlute chipFlute;
            chipFlute.edges = {edgeBI, edgeIC0};
            chipFlute.pStart = pB;
            chipFlute.pEnd = pC0;
            chipFlute.pStartLastEdge = pI;
            chipFlutes.emplace_back(chipFlute);

            return 0;
        }
        return -1;
    };

    const std::vector<Standard_Real>& teethAnglesVec = geomParam->GetTeethAngles();
    if (teethAnglesVec.size() != geomParam->GetTeethNumber()) {
        std::cerr << "Invalid teeth angles size: " << teethAnglesVec.size()
                  << " teeth number: " << geomParam->GetTeethNumber() << std::endl;

        return -1;
    }
    for (Standard_Size nT = 0; nT < teethAnglesVec.size(); nT++) {
        Standard_Real rotateAngle = lucMath::RadiansFromDegrees(teethAnglesVec[nT]);
        if (create_flute_edge(rotateAngle) != 0) {
            std::cerr << "create_flute_edge failed" << std::endl;
            return -1;
        }
    }

    if (cutFlag == -1) {
        if (chipFlutes.size() > 2) {
            std::reverse(chipFlutes.begin() + 1, chipFlutes.end());
        }
    }

    return 0;
}

void lucChipFluteModel::PrintSelf(ostream& os, lucIndent indent)
{
}

lucChipFluteModel::~lucChipFluteModel()
{
    std::cout << "destruct lucChipFluteModel" << std::endl;

    if (m_geomParam) {
        m_geomParam->Delete();
        m_geomParam = nullptr;
    }

    if (m_processParam) {
        m_processParam->Delete();
        m_processParam = nullptr;
    }

    if (m_gWheel) {
        m_gWheel->Delete();
        m_gWheel = nullptr;
    }

    // if (m_proStepBase) {
    //     m_proStepBase->Delete();
    //     m_proStepBase = nullptr;
    // }
    std::cout << "destruct lucChipFluteModel succeeded." << std::endl;
}

LUC_ABI_NAMESPACE_END
