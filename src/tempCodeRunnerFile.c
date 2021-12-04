clock_gettime(CLOCK_REALTIME, &ts);
        curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        int bytes_recv = recvfrom(s, header_recv, sizeof(header_t), MSG_DONTWAIT, ( struct sockaddr *) &si_other, &len);