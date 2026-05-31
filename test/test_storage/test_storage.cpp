#include <unity.h>
#include "../src/storage/StorageManager.h"
#include "../src/domain/IrrigationSystem.h"

#include <iostream>
#include <map>

extern std::map<String, String> mock_fs;
extern bool mock_fs_initialized;

extern uint8_t mock_pin_modes[256];
extern uint8_t mock_pin_states[256];

void setUp(void)
{
    mock_fs.clear();
    mock_fs_initialized = false;

    //Reinicio los mocks. Esto es necesario porque el mismo mock se comparte entre tests y el estado de un test podría afectar a otro si no se reinicia. Por ejemplo, si un test configura un pin como OUTPUT y lo deja en HIGH, el siguiente test podría encontrar ese pin ya configurado y con estado HIGH, lo que podría causar resultados inesperados o falsos positivos/negativos. Reiniciar los mocks garantiza que cada test comience con un estado limpio e independiente.
    for (int i = 0; i < 256; i++)
    {
        mock_pin_modes[i] = 0;
        mock_pin_states[i] = 0;
    }
}

void tearDown(void)
{
}

void test_storage_initialization(void)
{
    StorageManager storage;
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(mock_fs_initialized);
}

void test_load_programs_missing_file(void)
{
    StorageManager storage;
    IrrigationSystem sys;
    sys.begin();

    //Archivo no creado en mock.fs.
    TEST_ASSERT_FALSE(storage.loadPrograms(sys));
}

void test_save_and_load_programs(void)
{
    StorageManager storage;
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    //Crear un programa (modelo árbol: raíz + un hijo).
    Program p;
    p.setId(1);
    p.setStartTime("08:00");
    p.setDays(0b01010101);
    p.setCyclic(true);

    // {sectorId, irrigationTime, delay, parentSectorId, flow}
    ProgramNode n1 = {1, 600, 0, 0, 12};
    ProgramNode n2 = {2, 300, 5, 1, 6};
    p.addNode(n1);
    p.addNode(n2);
    p.setValid(true);

    //Agregar programa al sistema.
    sys.saveProgram(p);

    //Guardar en storage (mock_fs).
    TEST_ASSERT_TRUE(storage.savePrograms(sys));

    //Verificar que archivo exista en mock_fs.
    TEST_ASSERT_TRUE(mock_fs.count("/config.json") > 0);

    //Crear un nuevo sistema y cargar programas desde storage.
    IrrigationSystem sys2;
    sys2.begin();

    TEST_ASSERT_TRUE(storage.loadPrograms(sys2));

    //Verificar programa cargado verificando el primer programa cargado (debería ser el mismo que guardamos).
    const Program &loaded = sys2.programAt(0);
    TEST_ASSERT_TRUE(loaded.isValid());
    TEST_ASSERT_EQUAL(1, loaded.getId());
    TEST_ASSERT_EQUAL_STRING("08:00", loaded.getStartTime());
    TEST_ASSERT_EQUAL(0b01010101, loaded.getDays());
    TEST_ASSERT_TRUE(loaded.isCyclic());
    TEST_ASSERT_EQUAL(2, loaded.getSectorCount());

    //Raíz (sector 1).
    const ProgramNode *root = loaded.findNodeBySectorId(1);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(600, root->irrigationTime);
    TEST_ASSERT_EQUAL(0, root->delay);
    TEST_ASSERT_EQUAL(0, root->parentSectorId);
    TEST_ASSERT_EQUAL(12, root->flow);

    //Hijo (sector 2, cuelga del 1).
    const ProgramNode *child = loaded.findNodeBySectorId(2);
    TEST_ASSERT_NOT_NULL(child);
    TEST_ASSERT_EQUAL(300, child->irrigationTime);
    TEST_ASSERT_EQUAL(5, child->delay);
    TEST_ASSERT_EQUAL(1, child->parentSectorId);
    TEST_ASSERT_EQUAL(6, child->flow);

    //Estructura de árbol: 1 raíz, 1 hijo del sector 1.
    TEST_ASSERT_EQUAL(1, loaded.getRootCount());
    TEST_ASSERT_EQUAL(1, loaded.getChildCount(1));
}

void test_load_invalid_json(void)
{
    StorageManager storage;
    IrrigationSystem sys;
    sys.begin();

    mock_fs["/config.json"] = "{ invalid_json }";

    //Deberia devolver false al intentar cargar un JSON inválido.
    TEST_ASSERT_FALSE(storage.loadPrograms(sys));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_storage_initialization);
    RUN_TEST(test_load_programs_missing_file);
    RUN_TEST(test_save_and_load_programs);
    RUN_TEST(test_load_invalid_json);
    return UNITY_END();
}
