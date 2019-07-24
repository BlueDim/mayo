/****************************************************************************
** Copyright (c) 2019, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

// Need to include this first because of MSVC conflicts with M_E, M_LOG2, ...
#include <BRepPrimAPI_MakeBox.hxx>

#include "test.h"
#include "../src/brep_utils.h"
#include "../src/libtree.h"
#include "../src/mesh_utils.h"
#include "../src/result.h"
#include "../src/unit.h"
#include "../src/unit_system.h"

#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QtCore/QtDebug>
#include <cmath>
#include <cstring>
#include <utility>
#include <iostream>
#include <sstream>

namespace Mayo {

// For the sake of QCOMPARE()
static bool operator==(
        const UnitSystem::TranslateResult& lhs,
        const UnitSystem::TranslateResult& rhs)
{
    return std::abs(lhs.value - rhs.value) < 1e-6
            && std::strcmp(lhs.strUnit, rhs.strUnit) == 0
            && std::abs(lhs.factor - rhs.factor) < 1e-6;
}

void Test::BRepUtils_test()
{
    QVERIFY(BRepUtils::moreComplex(TopAbs_COMPOUND, TopAbs_SOLID));
    QVERIFY(BRepUtils::moreComplex(TopAbs_SOLID, TopAbs_SHELL));
    QVERIFY(BRepUtils::moreComplex(TopAbs_SHELL, TopAbs_FACE));
    QVERIFY(BRepUtils::moreComplex(TopAbs_FACE, TopAbs_EDGE));
    QVERIFY(BRepUtils::moreComplex(TopAbs_EDGE, TopAbs_VERTEX));

    {
        const TopoDS_Shape shapeNull;
        const TopoDS_Shape shapeBase = BRepPrimAPI_MakeBox(25, 25, 25);
        const TopoDS_Shape shapeCopy = shapeBase;
        QCOMPARE(BRepUtils::hashCode(shapeNull), -1);
        QVERIFY(BRepUtils::hashCode(shapeBase) >= 0);
        QCOMPARE(BRepUtils::hashCode(shapeBase), BRepUtils::hashCode(shapeCopy));
    }
}

void Test::CafUtils_test()
{
    // TODO Add CafUtils::labelTag() test for multi-threaded safety
}

void Test::MeshUtils_test()
{
    // Create box
    QFETCH(double, boxDx);
    QFETCH(double, boxDy);
    QFETCH(double, boxDz);
    const TopoDS_Shape shapeBox = BRepPrimAPI_MakeBox(boxDx, boxDy, boxDz);

    // Mesh box
    {
        BRepMesh_IncrementalMesh mesher(shapeBox, 0.1);
        mesher.Perform();
        QVERIFY(mesher.IsDone());
    }

    // Count nodes and triangles
    int countNode = 0;
    int countTriangle = 0;
    BRepUtils::forEachSubFace(shapeBox, [&](const TopoDS_Face& face) {
        TopLoc_Location loc;
        const Handle_Poly_Triangulation& polyTri = BRep_Tool::Triangulation(face, loc);
        if (!polyTri.IsNull()) {
            countNode += polyTri->NbNodes();
            countTriangle += polyTri->NbTriangles();
        }
    });

    // Merge all face triangulations into one
    Handle_Poly_Triangulation polyTriBox =
            new Poly_Triangulation(countNode, countTriangle, false);
    {
        int idNodeOffset = 0;
        int idTriangleOffset = 0;
        BRepUtils::forEachSubFace(shapeBox, [&](const TopoDS_Face& face) {
            TopLoc_Location loc;
            const Handle_Poly_Triangulation& polyTri = BRep_Tool::Triangulation(face, loc);
            if (!polyTri.IsNull()) {
                for (int i = 1; i <= polyTri->NbNodes(); ++i)
                    polyTriBox->ChangeNode(idNodeOffset + i) = polyTri->Node(i);

                for (int i = 1; i <= polyTri->NbTriangles(); ++i) {
                    int n1, n2, n3;
                    polyTri->Triangle(i).Get(n1, n2, n3);
                    polyTriBox->ChangeTriangle(idTriangleOffset + i).Set(
                                idNodeOffset + n1, idNodeOffset + n2, idNodeOffset + n3);
                }

                idNodeOffset += polyTri->NbNodes();
                idTriangleOffset += polyTri->NbTriangles();
            }
        });
    }

    // Checks
    QCOMPARE(MeshUtils::triangulationVolume(polyTriBox),
             double(boxDx * boxDy * boxDz));
    QCOMPARE(MeshUtils::triangulationArea(polyTriBox),
             double(2 * boxDx * boxDy + 2 * boxDy * boxDz + 2 * boxDx * boxDz));
}

void Test::MeshUtils_test_data()
{
    QTest::addColumn<double>("boxDx");
    QTest::addColumn<double>("boxDy");
    QTest::addColumn<double>("boxDz");

    QTest::newRow("case1") << 10. << 15. << 20.;
    QTest::newRow("case2") << 0.1 << 0.25 << 0.044;
}

void Test::Quantity_test()
{
    const QuantityArea area = (10 * Quantity_Millimeter) * (5 * Quantity_Centimeter);
    QCOMPARE(area.value(), 500.);
    QCOMPARE((Quantity_Millimeter / 5.).value(), 1/5.);
}

namespace Result_test {

struct Data {
    static std::ostream* data_ostr;

    Data() {
        *data_ostr << 0;
    }
    Data(const Data& other) : foo(other.foo) {
        *data_ostr << 1;
    }
    Data(Data&& other) {
        foo = std::move(other.foo);
        *data_ostr << 2;
    }
    Data& operator=(const Data& other) {
        this->foo = other.foo;
        *data_ostr << 3;
        return *this;
    }
    Data& operator=(Data&& other) {
        this->foo = std::move(other.foo);
        *data_ostr << 4;
        return *this;
    }
    QString foo;
};
std::ostream* Data::data_ostr = nullptr;

} // Result_test

void Test::Result_test()
{
    using Result = Result<Result_test::Data>;
    {
        std::ostringstream sstr;
        Result::Data::data_ostr = &sstr;
        const Result res = Result::error("error_description");
        QVERIFY(res.errorText() == "error_description");
        QVERIFY(!res.valid());
        QCOMPARE(sstr.str().c_str(), "02");
    }
    {
        std::ostringstream sstr;
        Result::Data::data_ostr = &sstr;
        Result::Data data;
        data.foo = "FooData";
        const Result res = Result::ok(std::move(data));
        QVERIFY(res.valid());
        QVERIFY(res.get().foo == "FooData");
        QCOMPARE(sstr.str().c_str(), "0042");
    }
}

void Test::UnitSystem_test()
{
    const UnitSystem::Schema schemaSI = UnitSystem::SI;
    using TranslateResult_Test =
        std::pair<UnitSystem::TranslateResult, UnitSystem::TranslateResult>;
    const TranslateResult_Test array[] = {
        { UnitSystem::translate(schemaSI, 80 * Quantity_Millimeter), { 80., "mm", 1. } },
        { UnitSystem::translate(schemaSI, 8 * Quantity_Centimeter),  { 80., "mm", 1. } },
        { UnitSystem::translate(schemaSI, 8 * Quantity_Meter),  { 8000., "mm", 1. } },
        { UnitSystem::translate(schemaSI, 0.5 * Quantity_SquaredCentimer), { 50., "mm²", 1. } }
    };
    for (const TranslateResult_Test& test : array) {
        QCOMPARE(test.first, test.second);
    }

    {
        const UnitSystem::TranslateResult tr =
                UnitSystem::degrees(3.14159265358979323846 * Quantity_Radian);
        QCOMPARE(tr.value, 180.);
    }
}

void Test::LibTree_test()
{
    const TreeNodeId nullptrId = 0;
    Tree<std::string> tree;
    TreeNodeId n0 = tree.appendChild(0, "0");
    TreeNodeId n0_1 = tree.appendChild(n0, "0-1");
    TreeNodeId n0_2 = tree.appendChild(n0, "0-2");
    TreeNodeId n0_1_1 = tree.appendChild(n0_1, "0-1-1");
    TreeNodeId n0_1_2 = tree.appendChild(n0_1, "0-1-2");

    QCOMPARE(tree.nodeParent(n0_1), n0);
    QCOMPARE(tree.nodeParent(n0_2), n0);
    QCOMPARE(tree.nodeParent(n0_1_1), n0_1);
    QCOMPARE(tree.nodeParent(n0_1_2), n0_1);
    QCOMPARE(tree.nodeChildFirst(n0_1), n0_1_1);
    QCOMPARE(tree.nodeChildLast(n0_1), n0_1_2);
    QCOMPARE(tree.nodeSiblingNext(n0_1_1), n0_1_2);
    QCOMPARE(tree.nodeSiblingPrevious(n0_1_2), n0_1_1);
    QCOMPARE(tree.nodeSiblingNext(n0_1_2), nullptrId);
}

} // namespace Mayo

