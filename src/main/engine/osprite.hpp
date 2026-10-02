/***************************************************************************
    Hardware Sprites.
    
    This class stores sprites in the converted format expected by the
    OutRun graphics hardware.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include "stdint.hpp" 

class osprite
{
public:
	uint16_t data[0x7];
	uint32_t scratch;

	osprite(void);
	~osprite(void);
    void init();

	uint16_t get_x();
	uint16_t get_y();
	void set_x(uint16_t);
	void inc_x(uint16_t);
	void set_y(uint16_t);
	void set_pitch(uint8_t);
	void set_vzoom(uint16_t);
	void set_hzoom(uint16_t);
	void set_priority(uint8_t);
	void set_offset(uint16_t o);
	void inc_offset(uint16_t o);
	void set_render(uint8_t b);
	void set_pal(uint8_t);
	void set_height(uint8_t);
	void sub_height(uint8_t);
	void set_bank(uint8_t);
	void hide();

private:

};

// Small accessors, defined inline (they are called about a dozen times per sprite per game step).
// X is now stored separately (not in the original data structure)
// This is to support wide-screen mode
inline uint16_t osprite::get_x()
{
    return data[0x6];
}

inline uint16_t osprite::get_y()
{
    return data[0x0]; // returning y uses whole value
}

inline void osprite::set_x(uint16_t x)
{
    data[0x6] = x;
}

inline void osprite::set_pitch(uint8_t p)
{
    data[0x02] = (data[0x2] & 0x1FF) | ((p & 0xFE) << 8);
}

inline void osprite::inc_x(uint16_t v)
{
    data[0x6] += v;
}

inline void osprite::set_y(uint16_t y)
{
    data[0x0] = y; // setting y wipes entire value
}

inline void osprite::set_vzoom(uint16_t z)
{
    data[0x03] = z;
}

inline void osprite::set_hzoom(uint16_t z)
{
    data[0x4] = z;
}

inline void osprite::set_priority(uint8_t p)
{
    data[0x03] |= (p << 8);
}

inline void osprite::set_offset(uint16_t o)
{
    data[0x1] = o;
}

inline void osprite::inc_offset(uint16_t o)
{
    data[0x1] += o;
}

inline void osprite::set_render(uint8_t bits)
{
    data[0x4] |= ((bits & 0xE0) << 8);
}

inline void osprite::set_pal(uint8_t pal)
{
    data[0x5] = (data[0x5] & 0xFF00) + pal;
}

inline void osprite::set_height(uint8_t h)
{
    data[0x5] = (data[0x5] & 0xFF) + (h << 8);
}

inline void osprite::sub_height(uint8_t h)
{
    uint8_t height = ((data[0x05] >> 8) - h) & 0xFF;
    data[0x5] = (data[0x5] & 0xFF) + (height << 8);
}

inline void osprite::set_bank(uint8_t bank)
{
    data[0x0] |= (bank << 8);
}

inline void osprite::hide(void)
{
    data[0x0] |= 0x4000;
    data[0x0] &= ~0x8000; // denote sprite list not ended
}
