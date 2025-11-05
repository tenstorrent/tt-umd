FROM fedora:39

COPY docker_rhel_install_common.sh /docker_rhel_install_common.sh
RUN chmod +x /docker_rhel_install_common.sh && /docker_rhel_install_common.sh


