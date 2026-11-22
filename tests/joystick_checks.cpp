#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "game/joystick.h"
#include "tests/reference_reader.h"

int main(int const argc, char const *const argv[]) {
  /// Match the original calibrated response and check that physical flight controls remain independent of keyboard head views
  if(argc != 2) throw std::invalid_argument{"joystick response fixture required"};
  namespace game = corncob::game;
  corncob::tests::reference_reader fixture{argv[1], "CORNJOY1"};
  auto const count{fixture.dword()};
  for(unsigned int i{0}; i != count; ++i) {
    auto const sample{fixture.signed_word()}, expected{fixture.signed_word()};
    if(game::joystick_axis(sample) != expected) throw std::runtime_error{"native joystick response case " + std::to_string(i)};
  }
  game::joystick_sample const joystick{
    .axes{32'767, -32'767},
    .gun{true},
    .missile{true},
  };
  game::input_state keyboard{
    .stick{
      .roll{256},
    },
  };
  game::apply_joystick(keyboard, joystick, false);
  if(keyboard.stick.roll != 256 || keyboard.stick.pitch != 0 || keyboard.joystick_controlled || !keyboard.weapons.gun || !keyboard.weapons.missile) {
    throw std::runtime_error{"keyboard priority changed independent joystick buttons"};
  }
  game::apply_joystick(keyboard, joystick, true);
  if(keyboard.stick.roll != 512 || keyboard.stick.pitch != -512 || !keyboard.joystick_controlled) {
    throw std::runtime_error{"head-view keys suppressed physical flight control"};
  }
  // Cover Space, Shift and C individually and together, with every combination of joystick fire buttons.
  for(unsigned int keys{0}; keys != 8; ++keys) for(unsigned int buttons{0}; buttons != 4; ++buttons) {
    game::input_state input{
      .weapons{
        .gun{(keys & 1u) != 0},
        .cannon{(keys & 2u) != 0},
        .missile{(keys & 4u) != 0},
      },
    };
    game::joystick_sample const sample{
      .gun{(buttons & 1u) != 0},
      .missile{(buttons & 2u) != 0},
    };
    game::apply_joystick(input, sample, false);
    if(input.weapons.gun != ((keys & 1u) != 0 || sample.gun)
      || input.weapons.cannon != ((keys & 2u) != 0)
      || input.weapons.missile != ((keys & 4u) != 0 || sample.missile)) {
      throw std::runtime_error{"joystick buttons changed a keyboard weapon binding"};
    }
  }
  std::cout << count << " native joystick responses match; keyboard priority, head views and 32 combined weapon inputs passed" << std::endl;
  return EXIT_SUCCESS;
}
