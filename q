SEM_INIT(3)                                                                               Linux Programmer's Manual                                                                               SEM_INIT(3)

NNAAMMEE
       sem_init - initialize an unnamed semaphore

SSYYNNOOPPSSIISS
       ##iinncclluuddee <<sseemmaapphhoorree..hh>>

       iinntt sseemm__iinniitt((sseemm__tt **_s_e_m,, iinntt _p_s_h_a_r_e_d,, uunnssiiggnneedd iinntt _v_a_l_u_e));;

       Link with _-_p_t_h_r_e_a_d.

DDEESSCCRRIIPPTTIIOONN
       sseemm__iinniitt() initializes the unnamed semaphore at the address pointed to by _s_e_m.  The _v_a_l_u_e argument specifies the initial value for the semaphore.

       The _p_s_h_a_r_e_d argument indicates whether this semaphore is to be shared between the threads of a process, or between processes.

       If  _p_s_h_a_r_e_d  has the value 0, then the semaphore is shared between the threads of a process, and should be located at some address that is visible to all threads (e.g., a global variable, or a vari‐
       able allocated dynamically on the heap).

       If _p_s_h_a_r_e_d is nonzero, then the semaphore is shared between processes, and should be located in a region of shared memory (see sshhmm__ooppeenn(3), mmmmaapp(2),  and  sshhmmggeett(2)).   (Since  a  child  created  by
       ffoorrkk(2)  inherits  its parent's memory mappings, it can also access the semaphore.)  Any process that can access the shared memory region can operate on the semaphore using sseemm__ppoosstt(3), sseemm__wwaaiitt(3),
       and so on.

       Initializing a semaphore that has already been initialized results in undefined behavior.

RREETTUURRNN VVAALLUUEE
       sseemm__iinniitt() returns 0 on success; on error, -1 is returned, and _e_r_r_n_o is set to indicate the error.

EERRRROORRSS
       EEIINNVVAALL _v_a_l_u_e exceeds SSEEMM__VVAALLUUEE__MMAAXX.

       EENNOOSSYYSS _p_s_h_a_r_e_d is nonzero, but the system does not support process-shared semaphores (see sseemm__oovveerrvviieeww(7)).

AATTTTRRIIBBUUTTEESS
       For an explanation of the terms used in this section, see aattttrriibbuutteess(7).

       ┌───────────┬───────────────┬─────────┐
       │IInntteerrffaaccee  │ AAttttrriibbuuttee     │ VVaalluuee   │
       ├───────────┼───────────────┼─────────┤
       │sseemm__iinniitt() │ Thread safety │ MT-Safe │
       └───────────┴───────────────┴─────────┘
CCOONNFFOORRMMIINNGG TTOO
       POSIX.1-2001.

NNOOTTEESS
       Bizarrely, POSIX.1-2001 does not specify the value that should be returned by a successful call to sseemm__iinniitt().  POSIX.1-2008 rectifies this, specifying the zero return on success.

SSEEEE AALLSSOO
       sseemm__ddeessttrrooyy(3), sseemm__ppoosstt(3), sseemm__wwaaiitt(3), sseemm__oovveerrvviieeww(7)

CCOOLLOOPPHHOONN
       This page is part of release 5.05 of the Linux _m_a_n_-_p_a_g_e_s project.  A description of the project,  information  about  reporting  bugs,  and  the  latest  version  of  this  page,  can  be  found  at
       https://www.kernel.org/doc/man-pages/.

Linux                                                                                             2017-09-15                                                                                      SEM_INIT(3)
