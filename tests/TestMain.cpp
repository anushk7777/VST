#include <juce_core/juce_core.h>

int main (int argc, char** argv)
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // LinenTests [name]: only the suites whose names contain name (all by default).
    if (argc > 1)
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* test : juce::UnitTest::getTestsInCategory ("Linen"))
            if (test->getName().containsIgnoreCase (argv[1]))
                chosen.add (test);
        runner.runTests (chosen);
    }
    else
    {
        runner.runTestsInCategory ("Linen");
    }

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
