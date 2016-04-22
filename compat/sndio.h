#ifdef HAVE_SNDIO
#include_next <sndio.h>
#else
  #ifdef HAVE_SYS_MIDIIO
  #include <sys/types.h>
  #define MIO_PORTANY	"default"
  #define MIO_OUT	4
  struct mio_hdl {
	int fd;
  };
  struct mio_hdl       *mio_open(const char *, unsigned int, int);
  size_t		mio_write(struct mio_hdl *, const void *, size_t);
  void			mio_close(struct mio_hdl *);
  #endif /* HAVE_SYS_MIDIIO */
#endif
