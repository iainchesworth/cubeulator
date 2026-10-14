// Catch2's usual Catch2::Catch2WithMain auto-generates main(), but Qt's
// meta-object machinery (signals/slots, NOTIFY properties) needs a
// QCoreApplication constructed first - so this target links plain
// Catch2::Catch2 and supplies that main() itself instead.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
